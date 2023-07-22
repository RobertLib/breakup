#include "enemy.h"
#include "enemies.h"
#include "../bricks/bricks.h"
#include "../lib/audio.h"
#include "../lib/camera.h"
#include "../lib/gfx.h"
#include "../paddle/paddle.h"

int enemySize(EnemyType type)
{
  return type == ENEMY_MINI ? GFX_ENEMY_MINI_SIZE : GFX_ENEMY_SIZE;
}

bool enemyIsTargetable(const Enemy *enemy)
{
  return enemy->active && enemy->spawnFade > 0;
}

static void randomizeVelocity(Enemy *enemy)
{
  enemy->vel.x = 0;
  enemy->vel.y = 0;

  while (fabsf(enemy->vel.x) < 0.25f || fabsf(enemy->vel.y) < 0.25f)
  {
    enemy->vel.x = frandRange(-1, 1);
    enemy->vel.y = frandRange(-1, 1);
  }

  enemy->vel = vec2Norm(enemy->vel, 1);
}

void initializeEnemy(Enemy *enemy, EnemyType type, float x, float y, int spawnerIndex)
{
  enemy->pos.x = x;
  enemy->pos.y = y;
  enemy->type = type;
  enemy->active = true;
  enemy->animTime = frand() * 6.28f;
  enemy->spawnFade = 0;
  enemy->spawnerIndex = spawnerIndex;

  switch (type)
  {
  case ENEMY_DRIFTER:
    enemy->speed = 95;
    randomizeVelocity(enemy);
    enemy->actionTimer = frandRange(2.5f, 5.0f);
    break;
  case ENEMY_DIVER:
    enemy->speed = 60;
    randomizeVelocity(enemy);
    enemy->actionTimer = frandRange(2.0f, 4.0f); // time until the dive
    break;
  case ENEMY_SPLITTER:
    enemy->speed = 70;
    randomizeVelocity(enemy);
    enemy->actionTimer = frandRange(2.5f, 5.0f);
    break;
  case ENEMY_SHOOTER:
    enemy->speed = 80;
    enemy->vel = (Vec2){(rand() % 2) ? 1.0f : -1.0f, 0};
    enemy->actionTimer = frandRange(2.5f, 4.5f); // shoot cooldown
    break;
  case ENEMY_MINI:
    enemy->speed = 150;
    randomizeVelocity(enemy);
    enemy->actionTimer = frandRange(1.5f, 3.0f);
    break;
  default:
    break;
  }
}

// One flip per axis however many bricks are struck, which is the rule the ball
// and the walls are written to as well.
//
// Flipping inside the loop meant two flips of vel.x on one frame cancelled out
// exactly and the enemy carried straight on through the wall - and two is the
// ordinary case rather than a corner one: bricks sit 52px apart and an enemy is
// 32px wide, so it overlaps two of them side by side for most of its travel
// across a row. The same cancellation is commented in ball.c twice, for the
// bricks and for the enemies; this is the third place it lived.
//
// The swept positions are taken from the velocity the frame started with, once,
// before anything is flipped, so the answer does not depend on which brick
// queryBricksNear() happened to return first.
static void bounceOffBricks(Enemy *enemy)
{
  int size = enemySize(enemy->type);

  Brick *nearBricks[32];
  int n = queryBricksNear(enemy->pos.x + size / 2.0f, enemy->pos.y + size / 2.0f,
                          nearBricks, 32);

  float nextX = enemy->pos.x + enemy->vel.x * enemy->speed * (float)dt;
  float nextY = enemy->pos.y + enemy->vel.y * enemy->speed * (float)dt;

  bool flipX = false;
  bool flipY = false;

  for (int i = 0; i < n; i++)
  {
    Brick *brick = nearBricks[i];

    // Already inside this brick - it moved, or the enemy spawned or drifted in.
    // The swept tests below would flip both axes on every frame it stayed
    // there and leave it jittering in place. Push it out along the axis it is
    // least deep in, turn that axis only if it was heading further in, and let
    // the swept tests deal with the rest.
    if (checkCollision(
            enemy->pos.x, enemy->pos.y,
            size, size,
            brick->pos.x, brick->pos.y,
            BRICK_WIDTH, BRICK_HEIGHT))
    {
      float dx = (enemy->pos.x + size / 2.0f) - (brick->pos.x + BRICK_WIDTH / 2.0f);
      float dy = (enemy->pos.y + size / 2.0f) - (brick->pos.y + BRICK_HEIGHT / 2.0f);
      float penX = (size + BRICK_WIDTH) / 2.0f - fabsf(dx);
      float penY = (size + BRICK_HEIGHT) / 2.0f - fabsf(dy);

      if (penX < penY)
      {
        enemy->pos.x += dx < 0 ? -penX : penX;
        if (enemy->vel.x * dx < 0)
        {
          enemy->vel.x *= -1;
        }
      }
      else
      {
        enemy->pos.y += dy < 0 ? -penY : penY;
        if (enemy->vel.y * dy < 0)
        {
          enemy->vel.y *= -1;
        }
      }

      continue;
    }

    if (checkCollision(
            nextX, enemy->pos.y,
            size, size,
            brick->pos.x, brick->pos.y,
            BRICK_WIDTH, BRICK_HEIGHT))
    {
      flipX = true;
    }

    if (checkCollision(
            enemy->pos.x, nextY,
            size, size,
            brick->pos.x, brick->pos.y,
            BRICK_WIDTH, BRICK_HEIGHT))
    {
      flipY = true;
    }
  }

  if (flipX)
  {
    enemy->vel.x *= -1;
  }
  if (flipY)
  {
    enemy->vel.y *= -1;
  }
}

void updateEnemy(Enemy *enemy)
{
  if (!enemy->active)
  {
    return;
  }

  int size = enemySize(enemy->type);

  // Asked before spawnFade is advanced on the next line, because this is the
  // one place that both reads the answer and makes it true. The paddle test at
  // the bottom is the last way a splitter's mini could still die on the frame
  // it was born: a splitter driven into by the paddle drops a mini onto the
  // paddle, and the loop in updateEnemies() reaches that mini's slot later in
  // the same pass. The ball, the laser and an explosion all ask
  // enemyIsTargetable() from outside and get `false` for free.
  bool targetable = enemyIsTargetable(enemy);

  enemy->animTime += (float)dt;
  enemy->spawnFade = fminf(1.0f, enemy->spawnFade + 2.0f * (float)dt);

  bool diving = enemy->type == ENEMY_DIVER && enemy->actionTimer <= 0;

  // Type-specific behavior
  enemy->actionTimer -= (float)dt;

  switch (enemy->type)
  {
  case ENEMY_DRIFTER:
  case ENEMY_SPLITTER:
  case ENEMY_MINI:
    if (enemy->actionTimer <= 0)
    {
      randomizeVelocity(enemy);
      enemy->actionTimer = frandRange(2.5f, 5.0f);
    }
    break;

  case ENEMY_DIVER:
    if (diving)
    {
      // Home in on the paddle with a serpentine wiggle
      float targetX = paddle.pos.x - size / 2.0f;
      float dirX = clamp((targetX - enemy->pos.x) / 120.0f, -0.6f, 0.6f);
      enemy->vel.x = dirX + sinf(enemy->animTime * 6.0f) * 0.35f;
      enemy->vel.y = 1.0f;
      enemy->vel = vec2Norm(enemy->vel, 1);
      enemy->speed = 190;
    }
    break;

  case ENEMY_SHOOTER:
    if (enemy->actionTimer <= 0)
    {
      // Only shoot while visible on screen
      if (enemy->pos.y > camera.y && enemy->pos.y < camera.y + SCREEN_HEIGHT - 150)
      {
        spawnEnemyBullet(enemy->pos.x + size / 2.0f, enemy->pos.y + size);
        playSfx(SFX_SHOOT);
      }
      enemy->actionTimer = frandRange(2.6f, 4.4f);
    }
    break;

  default:
    break;
  }

  // Wall bounces
  float nextX = enemy->pos.x + enemy->vel.x * enemy->speed * dt;
  float nextY = enemy->pos.y + enemy->vel.y * enemy->speed * dt;

  // The sign is forced away from the wall rather than flipped, which is the
  // rule the ceiling and the floor below are already written to, and the one
  // every wall in ball.c carries a paragraph about.
  //
  // A flip can be undone, and there is something here to undo it:
  // bounceOffBricks() runs a few lines down and flips vel.x again for a brick
  // met on the same frame, and two flips of one axis cancel exactly. That
  // would step the enemy through the wall it had just been turned away from -
  // and nothing below puts it back, because the despawn test measures y alone.
  // It would go on drifting off the side of the field, invisible, holding a
  // slot in the pool and a spawn point that never schedules a respawn.
  //
  // Forcing the sign cannot cancel with anything and cannot be applied twice.
  // The bricks are then free to turn the enemy back into the field, which is
  // the one direction that is always safe.
  if (nextX < 0)
  {
    enemy->vel.x = fabsf(enemy->vel.x);
  }
  else if (nextX + size > SCREEN_WIDTH)
  {
    enemy->vel.x = -fabsf(enemy->vel.x);
  }

  // The same ceiling the ball turns back at: the bottom of the status bar,
  // rather than the top of the window it is drawn over.
  if (nextY < camera.y + STATUS_BAR_HEIGHT && !diving)
  {
    enemy->vel.y = fabsf(enemy->vel.y);
  }

  if (!diving)
  {
    bounceOffBricks(enemy);
  }

  // Below the visible area: divers die quietly, others turn back up
  if (enemy->pos.y + size > camera.y + SCREEN_HEIGHT)
  {
    if (diving || enemy->type == ENEMY_MINI)
    {
      enemy->active = false;
      enemyDespawned(enemy);
      return;
    }

    enemy->vel.y = -fabsf(enemy->vel.y);
  }

  enemy->pos.x += enemy->vel.x * enemy->speed * dt;
  enemy->pos.y += enemy->vel.y * enemy->speed * dt;

  // Touching the paddle destroys the enemy
  if (targetable &&
      checkCollision(
          enemy->pos.x, enemy->pos.y, size, size,
          paddle.pos.x - paddleWidth() / 2.0f, paddle.pos.y,
          paddleWidth(), PADDLE_HEIGHT))
  {
    killEnemy(enemy, 50);
  }
}

void drawEnemy(const Enemy *enemy)
{
  if (!enemy->active)
  {
    return;
  }

  int size = enemySize(enemy->type);
  float screenY = enemy->pos.y - camera.y;

  if (screenY < -size - 20 || screenY > SCREEN_HEIGHT + 20)
  {
    return;
  }

  SDL_Texture *tex = texEnemy[enemy->type];
  Uint8 alpha = (Uint8)(enemy->spawnFade * 255);

  float angle = 0;
  float bob = 0;
  SDL_Color halo;

  switch (enemy->type)
  {
  case ENEMY_DRIFTER:
  case ENEMY_MINI:
    angle = enemy->animTime * 80.0f;
    halo = (SDL_Color){255, 90, 210, 255};
    break;
  case ENEMY_DIVER:
    angle = sinf(enemy->animTime * 5.0f) * 12.0f;
    halo = (SDL_Color){255, 145, 60, 255};
    break;
  case ENEMY_SPLITTER:
    angle = sinf(enemy->animTime * 3.0f) * 8.0f;
    halo = (SDL_Color){140, 245, 90, 255};
    break;
  case ENEMY_SHOOTER:
    bob = sinf(enemy->animTime * 2.5f) * 3.0f;
    halo = (SDL_Color){120, 190, 255, 255};
    break;
  default:
    halo = (SDL_Color){255, 255, 255, 255};
    break;
  }

  // Pulsing halo behind the enemy
  float pulse = 0.75f + 0.25f * sinf(enemy->animTime * 4.0f);
  float haloSize = size * 2.0f * pulse;

  SDL_SetTextureColorMod(texGlow, halo.r, halo.g, halo.b);
  SDL_SetTextureAlphaMod(texGlow, (Uint8)(60 * pulse * enemy->spawnFade));
  SDL_FRect haloRect = {
      enemy->pos.x + size / 2.0f - haloSize / 2,
      screenY + bob + size / 2.0f - haloSize / 2,
      haloSize,
      haloSize};
  SDL_RenderTexture(renderer, texGlow, NULL, &haloRect);
  SDL_SetTextureColorMod(texGlow, 255, 255, 255);
  SDL_SetTextureAlphaMod(texGlow, 255);

  SDL_SetTextureAlphaMod(tex, alpha);
  SDL_FRect dst = {enemy->pos.x, screenY + bob, (float)size, (float)size};
  SDL_RenderTextureRotated(renderer, tex, NULL, &dst, angle, NULL, SDL_FLIP_NONE);
  SDL_SetTextureAlphaMod(tex, 255);
}
