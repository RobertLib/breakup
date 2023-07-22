#include "paddle.h"
#include "../balls/balls.h"
#include "../boss/boss.h"
#include "../bricks/bricks.h"
#include "../bricks/brick-item.h"
#include "../enemies/enemies.h"
#include "../level-types.h"
#include "../level-manager.h"
#include "../lib/audio.h"
#include "../lib/camera.h"
#include "../lib/effects.h"
#include "../lib/game-state.h"
#include "../lib/gfx.h"
#include "../lib/particles.h"
#include "../screens/playing-screen.h"
#include "../ui/floating-text.h"

Paddle paddle;

static int requestedStartLevel = -1;
static int requestedStartScore = -1;

static const int DEFAULT_WIDTH = 100;
static const int LONG_WIDTH = 150;
static const int SHORT_WIDTH = 60;
static const int SPEED = 520;
static const int LIVES = 3;

static const float FRICTION = 10;
static const int PADDLE_CONTENT_GAP = 100;

// Returns the world-Y for the paddle so it sits below the level content
// but is never higher than the screen-bottom position (camera.y=0 case)
static int paddleYForLevel(int levelIndex)
{
  int contentY = getLevelContentBottom(levelIndex) + PADDLE_CONTENT_GAP;
  int minY = SCREEN_HEIGHT - PADDLE_HEIGHT - 30;
  return contentY > minY ? contentY : minY;
}

static void levelCompletedCallback(void *userdata);
static void lifeLossCallback(void *userdata);
static void readyCallback(void *userdata);
static void returnTypeToDefaultCallback(void *userdata);
static void shotCallback(void *userdata);
static void timeAfterStartCallback(void *userdata);

static Timer levelCompletedTimer = {0, 2.2f, &levelCompletedCallback, NULL};
static Timer lifeLossTimer = {0, 1, &lifeLossCallback, NULL};
static Timer readyTimer = {0, 2, &readyCallback, NULL};
static Timer returnTypeToDefaultTimer = {0, PADDLE_TYPE_DURATION, &returnTypeToDefaultCallback, NULL};
static Timer shotTimer = {0, 0.45f, &shotCallback, NULL};
static Timer timeAfterStartTimer = {0, 3, &timeAfterStartCallback, NULL};

static Bullet *findInactiveBullet(void)
{
  for (int i = 0; i < NUMBER_OF_BULLETS; i++)
  {
    if (!paddle.bullets[i].active)
    {
      return &paddle.bullets[i];
    }
  }

  return NULL;
}

// Reset paddle to initial state
static void reset(void)
{
  paddle.pos.x = SCREEN_WIDTH / 2;
  paddle.vel.x = 0;
  paddle.vel.y = 0;
  paddle.moveToX = -1;
  paddle.isReady = false;
  paddle.isTimeAfterStart = false;
  paddle.type = PADDLE_TYPE_DEFAULT;
  paddle.canShoot = true;
  paddle.inCollision = false;
  paddle.levelCompleted = false;

  readyTimer.elapsedTime = 0;
  levelCompletedTimer.elapsedTime = 0;
  timeAfterStartTimer.elapsedTime = 0;
}

static void respawnOrGameOver(void)
{
  if (paddle.lives > 1)
  {
    reset();

    paddle.lives--;

    resetBalls();
    resetEffects();
    resetBrickItems();
    resetCombo();
    clearEnemyBullets();
  }
  else
  {
    nextGameState = GAME_STATE_GAME_OVER_SCREEN;
  }
}

static void shoot(void)
{
  if (paddle.type != PADDLE_TYPE_SHOOTING || !paddle.canShoot)
  {
    return;
  }

  // Twin lasers from both turrets
  float offsets[2] = {
      -(paddleWidth() / 2.0f - 7.0f),
      paddleWidth() / 2.0f - 7.0f};

  bool fired = false;

  for (int i = 0; i < 2; i++)
  {
    Bullet *bullet = findInactiveBullet();

    if (bullet == NULL)
    {
      break;
    }

    bullet->pos.x = paddle.pos.x + offsets[i] - BULLET_WIDTH / 2.0f;
    bullet->pos.y = paddle.pos.y - 12;
    bullet->active = true;
    fired = true;

    spawnGlowPuff(paddle.pos.x + offsets[i], paddle.pos.y - 12,
                  (SDL_Color){255, 130, 110, 255}, 26, 0.15f);
  }

  if (fired)
  {
    playSfx(SFX_SHOOT);
    paddle.canShoot = false;
    shotTimer.elapsedTime = 0;
  }
}

void addScore(const int value)
{
  paddle.score += value;
}

void setStartLevel(int level)
{
  requestedStartLevel = level;
}

void setStartScore(int score)
{
  requestedStartScore = score;

  // Set straight away as well as remembered, because BREAKUP_STATE=gameover and
  // =win go to a screen that reads paddle.score without a paddle ever having
  // been initialized.
  paddle.score = score;
}

void changePaddleType(PaddleType type)
{
  if (type == PADDLE_TYPE_LONG || type == PADDLE_TYPE_SHOOTING ||
      type == PADDLE_TYPE_STICKY || type == PADDLE_TYPE_SHORT)
  {
    // Picking up (or refreshing) a timed type restarts its timer
    returnTypeToDefaultTimer.elapsedTime = 0;
  }

  paddle.type = type;
}

float paddleTypeTimeLeft(void)
{
  if (paddle.type == PADDLE_TYPE_DEFAULT || paddle.type == PADDLE_TYPE_DYING)
  {
    return 0;
  }

  return PADDLE_TYPE_DURATION - returnTypeToDefaultTimer.elapsedTime;
}

void paddleNextLevelUpdate(void)
{
  if (!paddle.nextLevel)
  {
    return;
  }

  // If the player has completed all levels
  if (paddle.level < getNumberOfLevels() - 1)
  {
    reset();

    // Move the paddle below the level content
    paddle.pos.y = paddleYForLevel(paddle.level + 1);

    resetBalls();
    resetEffects();
    resetBrickItems();
    resetCombo();
    clearParticles();
    clearFloatingTexts();

    destroyBricks();
    destroyEnemies();

    // Increase the level
    paddle.level++;

    initializeBricks();
    initializeEnemies();
    initializeBoss();
    initializeCamera();

    playMusic(musicForLevel(paddle.level));
  }
  else
  {
    nextGameState = GAME_STATE_WIN_SCREEN;
  }

  paddle.nextLevel = false;
}

void paddleTimerUpdate(void)
{
  if (paddle.levelCompleted)
  {
    updateTimer(&levelCompletedTimer);
  }
}

// Impact squash animation state (1 right after a hit, decays to 0)
static float impactT;

// For thruster particles: actual horizontal movement, including mouse moves
static float prevDrawX;

void paddleImpactKick(void)
{
  impactT = 1.0f;
}

int paddleWidth(void)
{
  switch (paddle.type)
  {
  case PADDLE_TYPE_LONG:
    return LONG_WIDTH;
  case PADDLE_TYPE_SHORT:
    return SHORT_WIDTH;
  default:
    return DEFAULT_WIDTH;
  }
}

void initializePaddle(void)
{
  reset();

  paddle.score = requestedStartScore > 0 ? requestedStartScore : 0;
  requestedStartScore = -1; // consumed, like the requested level below

  paddle.lives = LIVES;
  paddle.levelBonus = 0;

  // Use requested level if set, otherwise start at 0
  if (requestedStartLevel >= 0)
  {
    paddle.level = requestedStartLevel;
    requestedStartLevel = -1; // Reset for next time
  }
  else
  {
    paddle.level = 0;
  }

  paddle.pos.y = paddleYForLevel(paddle.level);
  paddle.nextLevel = false;

  // Initialize bullets
  for (int i = 0; i < NUMBER_OF_BULLETS; i++)
  {
    initializeBullet(&paddle.bullets[i]);
  }
}

static void updatePosition(void)
{
  if (isKeyDown[K_LEFT])
  {
    paddle.vel.x = -SPEED;
    paddle.moveToX = -1;
  }
  if (isKeyDown[K_RIGHT])
  {
    paddle.vel.x = SPEED;
    paddle.moveToX = -1;
  }

  // The paddle follows the mouse whenever it moves
  if (mouseMoved)
  {
    paddle.moveToX = motionX;
  }

  if (paddle.moveToX >= 0)
  {
    paddle.pos.x = lerp(paddle.pos.x, paddle.moveToX, fminf(1.0f, 18.0f * (float)dt));

    if (fabsf(paddle.pos.x - paddle.moveToX) < 0.5f)
    {
      paddle.moveToX = -1;
    }
  }

  // Update paddle position
  paddle.pos.x += paddle.vel.x * dt;
  // Apply friction
  paddle.vel.x /= 1 + FRICTION * dt;

  // Prevent paddle from going out of bounds
  int halfWidth = paddleWidth() / 2;

  if (paddle.pos.x - halfWidth < 0)
  {
    paddle.pos.x = halfWidth;
  }
  else if (paddle.pos.x + halfWidth > SCREEN_WIDTH)
  {
    paddle.pos.x = SCREEN_WIDTH - halfWidth;
  }
}

static void updateTimers(void)
{
  if (paddle.type == PADDLE_TYPE_DYING)
  {
    updateTimer(&lifeLossTimer);
  }
  else
  {
    lifeLossTimer.elapsedTime = 0;
  }

  if (!paddle.isReady)
  {
    updateTimer(&readyTimer);
  }

  if (paddle.type != PADDLE_TYPE_DEFAULT && paddle.type != PADDLE_TYPE_DYING)
  {
    updateTimer(&returnTypeToDefaultTimer);
  }
  else
  {
    returnTypeToDefaultTimer.elapsedTime = 0;
  }

  if (!paddle.canShoot)
  {
    updateTimer(&shotTimer);
  }

  if (paddle.isTimeAfterStart)
  {
    updateTimer(&timeAfterStartTimer);
  }
}

static void handleShooting(void)
{
  if (isKeyPressed[K_SPACE] || isMousePressed[1])
  {
    shoot();
  }
}

static void updateBullets(void)
{
  for (int i = 0; i < NUMBER_OF_BULLETS; i++)
  {
    updateBullet(&paddle.bullets[i]);
  }
}

static void checkPaddleCollision(void)
{
  // Through the brick grid rather than over every brick in the level, which is
  // what this was. The cost of the old loop was one checkCollision() and three
  // paddleWidth() switches per brick per frame, on a level that can hold well
  // over a thousand of them - to answer a question whose answer is no on every
  // ordinary level, because the paddle is parked a hundred pixels below the
  // last row. The grid looks at the six or so cells the paddle actually
  // touches.
  int width = paddleWidth();

  paddle.inCollision = bricksOverlapRect(
      paddle.pos.x - width / 2.0f, paddle.pos.y, width, PADDLE_HEIGHT);
}

static void updateThrusters(void)
{
  static float thrustTimer;

  float vx = dt > 0 ? (paddle.pos.x - prevDrawX) / (float)dt : 0;
  prevDrawX = paddle.pos.x;

  if (fabsf(vx) < 90.0f || paddle.type == PADDLE_TYPE_DYING)
  {
    return;
  }

  thrustTimer -= (float)dt;

  if (thrustTimer > 0)
  {
    return;
  }

  thrustTimer = 0.03f;

  // Exhaust puff at the trailing edge
  float dir = vx > 0 ? -1.0f : 1.0f;
  float x = paddle.pos.x + dir * (paddleWidth() / 2.0f - 4.0f);
  float y = paddle.pos.y + PADDLE_HEIGHT - 4.0f;

  spawnTrail(x, y + frandRange(-3, 3), (SDL_Color){140, 210, 255, 255},
             frandRange(9, 15));
}

void updatePaddle(void)
{
  updatePosition();
  updateTimers();
  handleShooting();
  updateBullets();
  updateThrusters();
  checkPaddleCollision();

  impactT = fmaxf(0.0f, impactT - 5.0f * (float)dt);
}

static SDL_Color paddleColor(void)
{
  switch (paddle.type)
  {
  case PADDLE_TYPE_LONG:
    return (SDL_Color){120, 255, 150, 255};
  case PADDLE_TYPE_SHOOTING:
    return (SDL_Color){255, 120, 110, 255};
  case PADDLE_TYPE_STICKY:
    return (SDL_Color){255, 220, 90, 255};
  case PADDLE_TYPE_SHORT:
    return (SDL_Color){255, 150, 60, 255};
  case PADDLE_TYPE_DYING:
    return (SDL_Color){160, 160, 170, 255};
  default:
    return (SDL_Color){110, 220, 255, 255};
  }
}

void drawPaddle(void)
{
  SDL_Texture *tex;

  switch (paddle.type)
  {
  case PADDLE_TYPE_LONG:
    tex = texPaddle[1];
    break;
  case PADDLE_TYPE_SHORT:
    tex = texPaddle[2];
    break;
  default:
    tex = texPaddle[0];
    break;
  }

  SDL_Color color = paddleColor();
  float screenY = paddle.pos.y - camera.y;
  float w = (float)paddleWidth();

  // Glow beneath the paddle
  SDL_SetTextureColorMod(texGlow, color.r, color.g, color.b);
  SDL_SetTextureAlphaMod(texGlow, 90);
  SDL_FRect glowRect = {
      paddle.pos.x - w * 0.75f,
      screenY + PADDLE_HEIGHT / 2.0f - w * 0.35f,
      w * 1.5f,
      w * 0.7f};
  SDL_RenderTexture(renderer, texGlow, NULL, &glowRect);
  SDL_SetTextureColorMod(texGlow, 255, 255, 255);
  SDL_SetTextureAlphaMod(texGlow, 255);

  Uint8 alpha = 255;
  if (paddle.type == PADDLE_TYPE_DYING)
  {
    alpha = (Uint8)clamp(255 - lifeLossTimer.elapsedTime * 255, 0, 255);
  }

  // Turrets for the laser paddle
  if (paddle.type == PADDLE_TYPE_SHOOTING)
  {
    SDL_FRect turretL = {paddle.pos.x - w / 2 + 1, screenY - 10, 12, 14};
    SDL_FRect turretR = {paddle.pos.x + w / 2 - 13, screenY - 10, 12, 14};
    SDL_RenderTexture(renderer, texTurret, NULL, &turretL);
    SDL_RenderTexture(renderer, texTurret, NULL, &turretR);
  }

  // Impact squash & stretch, centered on the paddle body
  float drawW = w * (1.0f + 0.10f * impactT);
  float drawH = PADDLE_HEIGHT * (1.0f - 0.25f * impactT);

  SDL_SetTextureColorMod(tex, color.r, color.g, color.b);
  SDL_SetTextureAlphaMod(tex, alpha);
  SDL_FRect dst = {
      paddle.pos.x - drawW / 2,
      screenY + (PADDLE_HEIGHT - drawH) / 2.0f,
      drawW,
      drawH};
  SDL_RenderTexture(renderer, tex, NULL, &dst);
  SDL_SetTextureColorMod(tex, 255, 255, 255);
  SDL_SetTextureAlphaMod(tex, 255);

  // Breathing neon core along the center line
  float pulse = 0.6f + 0.4f * sinf(gameTime * 3.2f);
  SDL_SetTextureColorMod(texGlow, color.r, color.g, color.b);
  SDL_SetTextureAlphaMod(texGlow, (Uint8)((36 + 26 * pulse) * alpha / 255));
  SDL_FRect core = {
      paddle.pos.x - drawW * 0.44f,
      screenY + PADDLE_HEIGHT / 2.0f - 8,
      drawW * 0.88f,
      16};
  SDL_RenderTexture(renderer, texGlow, NULL, &core);
  SDL_SetTextureColorMod(texGlow, 255, 255, 255);
  SDL_SetTextureAlphaMod(texGlow, 255);

  for (int i = 0; i < NUMBER_OF_BULLETS; i++)
  {
    drawBullet(&paddle.bullets[i]);
  }
}

void destroyPaddle(void)
{
}

// Callbacks

static void levelCompletedCallback(UNUSED void *userdata)
{
  paddle.levelCompleted = false;
  paddle.nextLevel = true;
}

static void lifeLossCallback(UNUSED void *userdata)
{
  paddle.type = PADDLE_TYPE_DEFAULT;

  respawnOrGameOver();
}

static void readyCallback(UNUSED void *userdata)
{
  paddle.isReady = true;
  paddle.isTimeAfterStart = true;
}

static void returnTypeToDefaultCallback(UNUSED void *userdata)
{
  changePaddleType(PADDLE_TYPE_DEFAULT);
}

static void shotCallback(UNUSED void *userdata)
{
  paddle.canShoot = true;
}

static void timeAfterStartCallback(UNUSED void *userdata)
{
  paddle.isTimeAfterStart = false;

  if (isBallsDocked())
  {
    releaseBalls();
  }
}
