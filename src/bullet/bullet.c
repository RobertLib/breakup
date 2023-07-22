#include "bullet.h"
#include "../boss/boss.h"
#include "../bricks/bricks.h"
#include "../enemies/enemies.h"
#include "../lib/camera.h"
#include "../lib/gfx.h"
#include "../lib/particles.h"
#include "../paddle/paddle.h"

static const int BULLET_HEIGHT = 16;
static const int SPEED = 700;

void initializeBullet(Bullet *bullet)
{
  bullet->pos.x = 0;
  bullet->pos.y = 0;
  bullet->active = false;
}

void updateBullet(Bullet *bullet)
{
  if (!bullet->active)
  {
    return;
  }

  bullet->pos.y -= SPEED * dt;

  Brick *nearBricks[32];
  int n = queryBricksNear(bullet->pos.x, bullet->pos.y, nearBricks, 32);

  for (int i = 0; i < n; i++)
  {
    Brick *brick = nearBricks[i];

    if (checkCollision(
            bullet->pos.x,
            bullet->pos.y,
            BULLET_WIDTH,
            BULLET_HEIGHT,
            brick->pos.x,
            brick->pos.y,
            BRICK_WIDTH,
            BRICK_HEIGHT))
    {
      bullet->active = false;
      damageBrick(brick, 1, false, (Vec2){0, -1});
      spawnGlowPuff(bullet->pos.x + BULLET_WIDTH / 2.0f, bullet->pos.y,
                    (SDL_Color){255, 150, 120, 255}, 30, 0.2f);
      break;
    }
  }

  if (!bullet->active)
  {
    return;
  }

  // The boss, which on a boss level is most of what there is to shoot: the
  // arena holds a dozen bricks and the rest of the field is the thing in the
  // middle of it. Without this a bolt flew straight through the armour and the
  // crystal alike, which made the laser capsule a dead pickup on seven levels
  // and looked like a bug on all of them.
  if (bossAbsorbsBullet(bullet->pos.x + BULLET_WIDTH / 2.0f,
                        bullet->pos.y + BULLET_HEIGHT / 2.0f,
                        BULLET_WIDTH / 2.0f))
  {
    bullet->active = false;
    return;
  }

  // Check enemy hits
  for (int i = 0; i < MAX_ENEMIES; i++)
  {
    Enemy *enemy = &enemies[i];

    if (!enemyIsTargetable(enemy))
    {
      continue;
    }

    int size = enemySize(enemy->type);

    if (checkCollision(
            bullet->pos.x,
            bullet->pos.y,
            BULLET_WIDTH,
            BULLET_HEIGHT,
            enemy->pos.x,
            enemy->pos.y,
            size,
            size))
    {
      bullet->active = false;
      killEnemy(enemy, 400);
      break;
    }
  }

  // Off the top of the *field*, which ends at the status bar rather than at
  // the top of the window - a bolt that carried on behind the HUD was a bolt
  // still in flight somewhere the player cannot see it.
  if (bullet->pos.y + BULLET_HEIGHT < camera.y + STATUS_BAR_HEIGHT)
  {
    bullet->active = false;
  }
}

void drawBullet(const Bullet *bullet)
{
  if (!bullet->active)
  {
    return;
  }

  float screenY = bullet->pos.y - camera.y;

  // Glow around the laser bolt
  SDL_SetTextureColorMod(texGlow, 255, 110, 90);
  SDL_SetTextureAlphaMod(texGlow, 160);
  SDL_FRect glow = {
      bullet->pos.x + BULLET_WIDTH / 2.0f - 14,
      screenY + BULLET_HEIGHT / 2.0f - 16,
      28,
      32};
  SDL_RenderTexture(renderer, texGlow, NULL, &glow);
  SDL_SetTextureColorMod(texGlow, 255, 255, 255);
  SDL_SetTextureAlphaMod(texGlow, 255);

  // Bright core
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 255, 230, 220, 255);
  SDL_FRect core = {
      bullet->pos.x + BULLET_WIDTH / 2.0f - 2,
      screenY,
      4,
      BULLET_HEIGHT};
  SDL_RenderFillRect(renderer, &core);
}
