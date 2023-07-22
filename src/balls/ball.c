#include "ball.h"
#include "balls.h"
#include "../boss/boss.h"
#include "../bricks/bricks.h"
#include "../enemies/enemies.h"
#include "../lib/audio.h"
#include "../lib/camera.h"
#include "../lib/effects.h"
#include "../lib/gfx.h"
#include "../lib/particles.h"
#include "../paddle/paddle.h"

static const float MAX_SPEED = 620;
static const float SPEED_PER_HIT = 4;
static const float MIN_VERTICAL_RATIO = 0.32f;

static float initSpeed(void)
{
  float speed = 380.0f + paddle.level * 5.0f;
  return fminf(speed, 500.0f);
}

static float effectiveSpeedFactor(void)
{
  return slowActive() ? 0.6f : 1.0f;
}

static void placeOnPaddle(Ball *ball)
{
  ball->pos.x = paddle.pos.x + ball->dockOffsetX - BALL_SIZE / 2.0f;
  ball->pos.y = paddle.pos.y - BALL_SIZE;
  ball->vel.x = 0;
  ball->vel.y = 0;
}

bool isBallDocked(const Ball *ball)
{
  return ball->docked;
}

void releaseBall(Ball *ball)
{
  if (!ball->docked)
  {
    return;
  }

  ball->docked = false;
  ball->speed = fmaxf(ball->speed, initSpeed());

  // Collapse the motion streak so it does not replay the pre-catch path
  for (int i = 0; i < BALL_HISTORY; i++)
  {
    ball->history[i] = ball->pos;
  }

  // Launch angle follows the dock position on the paddle
  float t = clamp(ball->dockOffsetX / (paddleWidth() / 2.0f), -1.0f, 1.0f);
  ball->vel.x = t * ball->speed * 0.8f + frandRange(-40, 40);
  ball->vel.y = -ball->speed;
  ball->vel = vec2Norm(ball->vel, ball->speed);

  ball->dockOffsetX = 0;
}

void initializeBall(Ball *ball, bool active)
{
  ball->active = active;
  ball->docked = true;
  ball->dockOffsetX = 0;
  ball->stickyTimer = 0;
  ball->trailTimer = 0;
  ball->speed = initSpeed();

  placeOnPaddle(ball);

  for (int i = 0; i < BALL_HISTORY; i++)
  {
    ball->history[i] = ball->pos;
  }
  ball->historyHead = 0;
}

// Keeps the ball from grinding along a nearly horizontal path
static void enforceMinVerticalAngle(Ball *ball)
{
  float minY = ball->speed * MIN_VERTICAL_RATIO;

  if (fabsf(ball->vel.y) < minY)
  {
    ball->vel.y = (ball->vel.y < 0 ? -1.0f : 1.0f) * minY;
    ball->vel = vec2Norm(ball->vel, ball->speed);
  }
}

static void increaseSpeed(Ball *ball)
{
  if (ball->speed < MAX_SPEED)
  {
    ball->speed += SPEED_PER_HIT;
  }
}

static void handleBrickCollisions(Ball *ball, float stepX, float stepY)
{
  Brick *nearBricks[32];
  int n = queryBricksNear(ball->pos.x + BALL_SIZE / 2.0f,
                          ball->pos.y + BALL_SIZE / 2.0f,
                          nearBricks, 32);

  if (n == 0)
  {
    return;
  }

  bool fire = fireballActive();
  bool flipX = false;
  bool flipY = false;

  Brick *hit[8];
  int hitCount = 0;

  for (int i = 0; i < n; i++)
  {
    Brick *brick = nearBricks[i];
    bool touched = false;

    if (fire && brick->kind != BRICK_SOLID)
    {
      // Fireball smashes straight through destructible bricks
      if (checkCollision(
              ball->pos.x + stepX, ball->pos.y + stepY, BALL_SIZE, BALL_SIZE,
              brick->pos.x, brick->pos.y, BRICK_WIDTH, BRICK_HEIGHT))
      {
        damageBrick(brick, 99, true, vec2Norm(ball->vel, 1.0f));
        increaseSpeed(ball);
      }
      continue;
    }

    if (checkCollision(
            ball->pos.x + stepX, ball->pos.y, BALL_SIZE, BALL_SIZE,
            brick->pos.x, brick->pos.y, BRICK_WIDTH, BRICK_HEIGHT))
    {
      flipX = true;
      touched = true;
    }

    if (checkCollision(
            ball->pos.x, ball->pos.y + stepY, BALL_SIZE, BALL_SIZE,
            brick->pos.x, brick->pos.y, BRICK_WIDTH, BRICK_HEIGHT))
    {
      flipY = true;
      touched = true;
    }

    if (touched && hitCount < 8)
    {
      hit[hitCount++] = brick;
    }
  }

  // Taken before the flips below, because the debris should follow the ball in
  // the direction it arrived rather than the one it leaves in.
  Vec2 impact = vec2Norm(ball->vel, 1.0f);

  // Flip each axis at most once, no matter how many bricks were struck
  if (flipX)
  {
    ball->vel.x *= -1;
  }
  if (flipY)
  {
    ball->vel.y *= -1;
  }

  for (int i = 0; i < hitCount; i++)
  {
    damageBrick(hit[i], 1, true, impact);
    increaseSpeed(ball);
  }
}

// The boss is a circle and a ring of circles rather than a grid of boxes, so it
// reflects properly about the surface normal instead of flipping an axis. That
// is also what makes the fight read: a ball coming off the curve of a plate
// leaves at the angle it arrived at, and the player can aim with it.
static void handleBossCollision(Ball *ball, float nextX, float nextY)
{
  Vec2 normal;
  float push;

  BossHit hit = bossCollide(nextX + BALL_SIZE / 2.0f, nextY + BALL_SIZE / 2.0f,
                            BALL_SIZE / 2.0f, ball->vel, &normal, &push);

  if (hit == BOSS_HIT_NONE)
  {
    return;
  }

  float dot = ball->vel.x * normal.x + ball->vel.y * normal.y;
  ball->vel.x -= 2.0f * dot * normal.x;
  ball->vel.y -= 2.0f * dot * normal.y;

  // Lifted clear of the surface as well as turned around. The boss moves under
  // its own steam, so a ball that was merely reflected could still be inside it
  // next frame - and then be reflected back in.
  ball->pos.x += normal.x * (push + 1.0f);
  ball->pos.y += normal.y * (push + 1.0f);

  // A hit on the crystal counts towards the combo the same way a brick does; it
  // is the only thing to hit on a boss level. A bounce off a plate or a shut
  // crystal is not a hit - it neither builds the combo nor, since it is not a
  // paddle touch, breaks it.
  if (hit == BOSS_HIT_CORE)
  {
    registerBallBrickBreak();
  }
}

// Returns true if the ball was caught by a sticky paddle, which is the one
// outcome that ends the frame for it: placeOnPaddle() has just zeroed the
// velocity, and everything updateBall() does after this point - normalizing,
// enforcing a minimum vertical angle, advancing the position - would take that
// zero and turn it into a downward step off the paddle for one frame.
static bool handlePaddleCollision(Ball *ball, float nextX, float nextY)
{
  float paddleLeft = paddle.pos.x - paddleWidth() / 2.0f;
  float paddleTop = paddle.pos.y;

  if (!checkCollision(
          nextX, nextY, BALL_SIZE, BALL_SIZE,
          paddleLeft, paddleTop, paddleWidth(), PADDLE_HEIGHT))
  {
    return false;
  }

  // Only react if the ball is coming from above
  if (ball->vel.y <= 0)
  {
    return false;
  }

  resetCombo();

  float ballCenterX = ball->pos.x + BALL_SIZE / 2.0f;
  float hitPosition = clamp(
      (ballCenterX - paddle.pos.x) / (paddleWidth() / 2.0f), -1.0f, 1.0f);

  // Sticky paddle catches the ball
  if (paddle.type == PADDLE_TYPE_STICKY)
  {
    ball->docked = true;
    ball->dockOffsetX = hitPosition * (paddleWidth() / 2.0f - 12.0f);
    ball->stickyTimer = 0;
    placeOnPaddle(ball);
    paddleImpactKick();
    playSfx(SFX_CATCH);
    return true;
  }

  ball->vel.x = hitPosition * ball->speed;
  ball->vel.y = -ball->speed;
  ball->vel = vec2Norm(ball->vel, ball->speed);
  enforceMinVerticalAngle(ball);

  // Move ball above paddle to prevent getting stuck
  ball->pos.y = paddleTop - BALL_SIZE;

  paddleImpactKick();
  playSfx(SFX_BALL_PADDLE);
  spawnGlowPuff(ballCenterX, paddleTop, (SDL_Color){140, 230, 255, 255}, 34, 0.2f);

  return false;
}

static void handleEnemyCollisions(Ball *ball, float stepX, float stepY)
{
  // Each axis tested on its own, and each flipped at most once, which is the
  // rule handleBrickCollisions() above is written to and this now shares.
  //
  // Two things come out of the separate tests. One flip however many enemies
  // are struck: flipping inside the loop meant two caught on one frame
  // cancelled each other out exactly and the ball carried straight on through
  // both - the identical cancellation the wall code below carries a comment
  // about, and it takes a cluster to see (a splitter's two minis land about
  // 24px apart, which leaves a couple of pixels where a ball overlaps both).
  //
  // And the ball comes off the side an enemy was actually met on. This tested
  // the diagonal step and turned `vel.y` around whatever it found, so a ball
  // travelling sideways into a drifter reversed its vertical direction and
  // sailed on past horizontally. Vertical approaches - which is most of them,
  // and why it went unnoticed - came out the same either way.
  bool flipX = false;
  bool flipY = false;

  Enemy *hit[MAX_ENEMIES];
  int hitCount = 0;

  for (int i = 0; i < MAX_ENEMIES; i++)
  {
    Enemy *enemy = &enemies[i];

    // enemyIsTargetable() rather than ->active, and it is what keeps this loop
    // from eating the minis a splitter it just killed dropped into the slots
    // ahead of it. See the note on the function in enemy.h.
    if (!enemyIsTargetable(enemy))
    {
      continue;
    }

    int size = enemySize(enemy->type);
    bool touched = false;

    if (checkCollision(
            ball->pos.x + stepX, ball->pos.y, BALL_SIZE, BALL_SIZE,
            enemy->pos.x, enemy->pos.y, size, size))
    {
      flipX = true;
      touched = true;
    }

    if (checkCollision(
            ball->pos.x, ball->pos.y + stepY, BALL_SIZE, BALL_SIZE,
            enemy->pos.x, enemy->pos.y, size, size))
    {
      flipY = true;
      touched = true;
    }

    if (touched)
    {
      hit[hitCount++] = enemy;
    }
  }

  if (flipX)
  {
    ball->vel.x *= -1;
  }
  if (flipY)
  {
    ball->vel.y *= -1;
  }

  // Killed after the scan rather than inside it. A splitter breaking up hands
  // its two minis to findInactiveEnemy(), which hands back the slot it has
  // just freed and any other - and every slot still waiting in `hit` is still
  // active, so none of them can be handed out from under this loop.
  for (int i = 0; i < hitCount; i++)
  {
    killEnemy(hit[i], 300);
  }
}

void updateBall(Ball *ball)
{
  if (!ball->active)
  {
    return;
  }

  if (ball->docked)
  {
    placeOnPaddle(ball);

    // Sticky catches release automatically after a moment
    if (paddle.isReady)
    {
      ball->stickyTimer += (float)dt;

      if (ball->stickyTimer > 3.0f)
      {
        releaseBall(ball);
        return;
      }
    }

    // Release the ball
    if ((isKeyPressed[K_SPACE] || isMousePressed[1]) &&
        paddle.isReady &&
        !paddle.inCollision &&
        !isCameraMoving())
    {
      releaseBall(ball);
    }

    return;
  }

  float factor = effectiveSpeedFactor();
  float stepX = ball->vel.x * factor * dt;
  float stepY = ball->vel.y * factor * dt;
  float nextX = ball->pos.x + stepX;
  float nextY = ball->pos.y + stepY;

  handleBrickCollisions(ball, stepX, stepY);
  handleBossCollision(ball, nextX, nextY);

  if (handlePaddleCollision(ball, nextX, nextY))
  {
    return; // caught by a sticky paddle; it belongs to the paddle now
  }

  handleEnemyCollisions(ball, stepX, stepY);

  // The walls come *after* the bricks, and each one sets a sign rather than
  // flipping one.
  //
  // A ball in the corner of the field can meet a wall and a brick on the same
  // frame, and two flips of vel.x in one frame cancel out exactly: the ball
  // carried straight on through the wall, and the frame after that it was far
  // enough out for updateBalls() to call it lost. Forcing the sign away from
  // the surface cannot cancel with anything and cannot be applied twice, and
  // the recomputed step below is the post-collision one rather than the
  // velocity the frame started with.
  float wallX = ball->pos.x + ball->vel.x * factor * (float)dt;
  float wallY = ball->pos.y + ball->vel.y * factor * (float)dt;

  if (wallX < 0 && ball->vel.x < 0)
  {
    ball->vel.x = fabsf(ball->vel.x);
    playSfx(SFX_BALL_WALL);
    spawnGlowPuff(4, ball->pos.y + BALL_SIZE / 2.0f,
                  (SDL_Color){160, 200, 255, 255}, 26, 0.18f);
  }
  else if (wallX + BALL_SIZE > SCREEN_WIDTH && ball->vel.x > 0)
  {
    ball->vel.x = -fabsf(ball->vel.x);
    playSfx(SFX_BALL_WALL);
    spawnGlowPuff(SCREEN_WIDTH - 4, ball->pos.y + BALL_SIZE / 2.0f,
                  (SDL_Color){160, 200, 255, 255}, 26, 0.18f);
  }

  // The ceiling is the bottom of the status bar, not the top of the window:
  // the panel is drawn over the field, so a ball that turned back at y = 0 did
  // it three-quarters hidden behind it. See STATUS_BAR_HEIGHT in globals.h.
  if (wallY < camera.y + STATUS_BAR_HEIGHT && ball->vel.y < 0)
  {
    ball->vel.y = fabsf(ball->vel.y);
    playSfx(SFX_BALL_WALL);
    spawnGlowPuff(ball->pos.x + BALL_SIZE / 2.0f,
                  camera.y + STATUS_BAR_HEIGHT,
                  (SDL_Color){160, 200, 255, 255}, 26, 0.18f);
  }

  // Barrier at the bottom
  if (shieldActive() && ball->vel.y > 0 &&
      wallY + BALL_SIZE >= camera.y + SCREEN_HEIGHT - 14)
  {
    ball->vel.y = -fabsf(ball->vel.y);
    playSfx(SFX_SHIELD);
    spawnGlowPuff(ball->pos.x + BALL_SIZE / 2.0f, camera.y + SCREEN_HEIGHT - 14,
                  (SDL_Color){190, 130, 255, 255}, 60, 0.3f);
  }

  // Normalize the velocity and advance
  ball->vel = vec2Norm(ball->vel, ball->speed);
  enforceMinVerticalAngle(ball);

  ball->pos.x += ball->vel.x * factor * dt;
  ball->pos.y += ball->vel.y * factor * dt;

  // Record the position for the motion streak
  ball->historyHead = (ball->historyHead + 1) % BALL_HISTORY;
  ball->history[ball->historyHead] = ball->pos;

  // Trail
  ball->trailTimer -= (float)dt;

  if (ball->trailTimer <= 0)
  {
    ball->trailTimer = 0.022f;

    SDL_Color trailColor = fireballActive()
                               ? (SDL_Color){255, 150, 50, 255}
                               : (SDL_Color){110, 200, 255, 255};

    spawnTrail(ball->pos.x + BALL_SIZE / 2.0f,
               ball->pos.y + BALL_SIZE / 2.0f,
               trailColor,
               fireballActive() ? 20 : 13);
  }
}

float ballCameraPull(const Ball *ball)
{
  if (!ball->active || ball->docked || camera.y <= 0 || ball->vel.y >= 0 ||
      ball->pos.y >= camera.y + SCREEN_HEIGHT / 4.0f)
  {
    return 0;
  }

  return ball->vel.y * effectiveSpeedFactor() * (float)dt;
}

void drawBall(const Ball *ball)
{
  if (!ball->active)
  {
    return;
  }

  bool fire = fireballActive();
  float screenY = ball->pos.y - camera.y;

  SDL_Color glowColor = fire ? (SDL_Color){255, 150, 60, 255}
                             : (SDL_Color){120, 210, 255, 255};

  // Afterimage streak along the recent flight path
  if (!ball->docked)
  {
    SDL_SetTextureColorMod(texBall, glowColor.r, glowColor.g, glowColor.b);

    for (int i = 0; i < BALL_HISTORY - 1; i++)
    {
      // Oldest first, skipping the newest slot (the ball itself)
      int idx = (ball->historyHead + 1 + i) % BALL_HISTORY;
      float t = (i + 1) / (float)BALL_HISTORY;

      float size = BALL_SIZE * (0.3f + 0.55f * t);
      float hx = ball->history[idx].x + BALL_SIZE / 2.0f;
      float hy = ball->history[idx].y - camera.y + BALL_SIZE / 2.0f;

      SDL_SetTextureAlphaMod(texBall, (Uint8)(80 * t * t));
      SDL_FRect ghost = {hx - size / 2, hy - size / 2, size, size};
      SDL_RenderTexture(renderer, texBall, NULL, &ghost);
    }
  }

  // Glow halo
  SDL_SetTextureColorMod(texGlow, glowColor.r, glowColor.g, glowColor.b);
  SDL_SetTextureAlphaMod(texGlow, fire ? 190 : 120);
  float glowSize = fire ? 52 : 40;
  SDL_FRect glow = {
      ball->pos.x + BALL_SIZE / 2.0f - glowSize / 2,
      screenY + BALL_SIZE / 2.0f - glowSize / 2,
      glowSize,
      glowSize};
  SDL_RenderTexture(renderer, texGlow, NULL, &glow);
  SDL_SetTextureColorMod(texGlow, 255, 255, 255);
  SDL_SetTextureAlphaMod(texGlow, 255);

  if (isBallDocked(ball) && isCameraMoving())
  {
    SDL_SetTextureAlphaMod(texBall, 255 / 2);
  }
  else
  {
    SDL_SetTextureAlphaMod(texBall, 255);
  }

  if (fire)
  {
    SDL_SetTextureColorMod(texBall, 255, 180, 90);
  }
  else
  {
    SDL_SetTextureColorMod(texBall, 235, 250, 255);
  }

  if (ball->docked)
  {
    SDL_FRect dstRect = {ball->pos.x, screenY, BALL_SIZE, BALL_SIZE};
    SDL_RenderTexture(renderer, texBall, NULL, &dstRect);
  }
  else
  {
    // Subtle squash & stretch along the direction of travel
    float speed01 = clamp(ball->speed / 620.0f, 0.0f, 1.0f);
    float w = BALL_SIZE * (1.0f + 0.14f * speed01);
    float h = BALL_SIZE * (1.0f - 0.08f * speed01);
    float angle = atan2f(ball->vel.y, ball->vel.x) * 57.2958f;

    SDL_FRect dstRect = {
        ball->pos.x + BALL_SIZE / 2.0f - w / 2,
        screenY + BALL_SIZE / 2.0f - h / 2,
        w,
        h};
    SDL_RenderTextureRotated(renderer, texBall, NULL, &dstRect, angle,
                             NULL, SDL_FLIP_NONE);
  }

  SDL_SetTextureColorMod(texBall, 255, 255, 255);
  SDL_SetTextureAlphaMod(texBall, 255);
}
