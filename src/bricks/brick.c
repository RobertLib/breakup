#include "brick.h"
#include "bricks.h"
#include "../level-types.h"
#include "../lib/camera.h"
#include "../lib/gfx.h"
#include "../paddle/paddle.h"
#include "../types.h"

const int BRICK_HEIGHT = LEVEL_PATTERN_ROW_HEIGHT;

void initializeBrick(Brick *brick, float x, float y, BrickKind kind, int row)
{
  brick->pos.x = x;
  brick->pos.y = y;
  brick->kind = kind;
  brick->active = true;
  brick->spawnDelay = 0;
  brick->spawnT = 0;
  brick->hitFlash = 0;

  int world = worldForLevel(paddle.level);
  static const float worldHueBase[WORLD_COUNT] = {190, 110, 10, 275};

  switch (kind)
  {
  case BRICK_BASIC:
    brick->maxHp = 1;
    brick->value = 100;
    brick->tint = hsvColor(worldHueBase[world] + row * 41.0f, 0.72f, 1.0f);
    break;
  case BRICK_DURABLE:
    brick->maxHp = 2;
    brick->value = 200;
    brick->tint = (SDL_Color){175, 195, 220, 255};
    break;
  case BRICK_TOUGH:
    brick->maxHp = 3;
    brick->value = 300;
    brick->tint = (SDL_Color){125, 140, 175, 255};
    break;
  case BRICK_SOLID:
    brick->maxHp = 999;
    brick->value = 0;
    brick->tint = (SDL_Color){255, 255, 255, 255};
    break;
  case BRICK_EXPLOSIVE:
    brick->maxHp = 1;
    brick->value = 150;
    brick->tint = (SDL_Color){255, 145, 70, 255};
    break;
  case BRICK_GOLD:
    brick->maxHp = 1;
    brick->value = 500;
    brick->tint = (SDL_Color){255, 255, 255, 255};
    break;
  case BRICK_FINAL:
    brick->maxHp = 1;
    brick->value = 1000;
    brick->tint = worldThemes[world].glow;
    break;
  }

  brick->hp = brick->maxHp;
}

static SDL_Texture *textureForKind(BrickKind kind)
{
  switch (kind)
  {
  case BRICK_SOLID:
    return texBrickSolid;
  case BRICK_GOLD:
    return texBrickGold;
  case BRICK_FINAL:
    return texCrystal;
  default:
    return texBrick;
  }
}

// Idle wave phase for a brick, derived from where it sits in the wall so that
// neighbours end up slightly out of step and the field ripples diagonally
// instead of pulsing as one block. Computed rather than stored, so there is
// nothing to keep in sync as bricks are built and destroyed.
static float brickPhase(const Brick *brick)
{
  return brick->pos.x * 0.020f + brick->pos.y * 0.035f;
}

// A specular glint that sweeps across the wall on a loop. It runs in screen
// space, so it reads the same however far the camera has scrolled.
#define BRICK_SHINE_PERIOD 5.0f
#define BRICK_SHINE_WIDTH 170.0f

static float brickShine(float x, float screenY)
{
  float span = SCREEN_WIDTH + SCREEN_HEIGHT * 0.5f;
  float head = -BRICK_SHINE_WIDTH +
               fmodf(gameTime, BRICK_SHINE_PERIOD) / BRICK_SHINE_PERIOD *
                   (span + 2 * BRICK_SHINE_WIDTH);
  float d = fabsf((x + screenY * 0.5f) - head);

  if (d >= BRICK_SHINE_WIDTH)
  {
    return 0;
  }

  float k = 1.0f - d / BRICK_SHINE_WIDTH;

  return k * k * k; // narrow the band into a highlight rather than a wash
}

void drawBrick(const Brick *brick)
{
  if (!brick->active || brick->spawnT <= 0)
  {
    return;
  }

  float screenY = brick->pos.y - camera.y;

  if (screenY < -BRICK_HEIGHT - 30 || screenY > SCREEN_HEIGHT + 30)
  {
    return;
  }

  SDL_Texture *tex = textureForKind(brick->kind);
  float phase = brickPhase(brick);
  int damage = brick->maxHp - brick->hp;

  // Level-intro pop-in with a small overshoot
  float scale = brick->spawnT >= 1.0f ? 1.0f : easeOutBack(brick->spawnT);
  Uint8 alpha = (Uint8)(fminf(1.0f, brick->spawnT * 2.5f) * 255);

  // Idle motion. All of it is presentation only - collisions keep reading
  // brick->pos - so it stays within a couple of pixels of where the brick is.
  float wave = sinf(gameTime * 1.5f - phase);
  float angle = 0;
  float offsetX = 0;
  float offsetY = 0;

  if (brick->kind == BRICK_FINAL)
  {
    // The objective crystal hovers and sways on its own, longer rhythm
    offsetY = sinf(gameTime * 1.8f + phase) * 2.5f;
    angle = sinf(gameTime * 1.15f + phase) * 6.0f;
  }
  else if (brick->kind != BRICK_SOLID)
  {
    offsetY = wave * 1.4f;
    angle = wave * 1.2f;
  }
  // Indestructible bricks hold still on purpose: being the one thing in the
  // wall that never moves is part of how they read as immovable.

  // Cracked bricks tremble, harder the closer they are to breaking
  if (damage > 0)
  {
    offsetX += sinf(gameTime * 27.0f + phase * 7.0f) * 0.5f * damage;
    offsetY += cosf(gameTime * 31.0f + phase * 5.0f) * 0.4f * damage;
  }

  // Hit reaction: quick horizontal stretch + vertical squash
  float flash = brick->hitFlash;
  float scaleX = scale * (1.0f + 0.10f * flash);
  float scaleY = scale * (1.0f - 0.16f * flash);

  float w = BRICK_WIDTH * scaleX;
  float h = BRICK_HEIGHT * scaleY;

  float restY = screenY + (BRICK_HEIGHT - h) / 2.0f;

  SDL_FRect dst = {
      brick->pos.x + (BRICK_WIDTH - w) / 2.0f + offsetX,
      restY + offsetY,
      w,
      h};

  // Soft drop shadow. It stays anchored to the brick's resting position rather
  // than following the bobbing sprite, so the gap opens up as the brick lifts.
  if (brick->kind != BRICK_FINAL)
  {
    SDL_SetTextureColorMod(tex, 0, 0, 0);
    SDL_SetTextureAlphaMod(tex, (Uint8)(70 * alpha / 255));
    SDL_FRect shadow = {
        brick->pos.x + (BRICK_WIDTH - w) / 2.0f + offsetX * 0.5f + 4,
        restY + 5,
        dst.w,
        dst.h};
    SDL_RenderTexture(renderer, tex, NULL, &shadow);
  }
  else
  {
    // Crystals glow instead of casting shadows
    float pulse = 0.75f + 0.25f * sinf(gameTime * 3.0f + brick->pos.x * 0.05f);
    float size = 74 * pulse * scale;
    SDL_SetTextureColorMod(texGlow, brick->tint.r, brick->tint.g, brick->tint.b);
    SDL_SetTextureAlphaMod(texGlow, (Uint8)(130 * pulse));
    SDL_FRect glow = {
        dst.x + dst.w / 2.0f - size / 2,
        dst.y + dst.h / 2.0f - size / 2,
        size,
        size};
    SDL_RenderTexture(renderer, texGlow, NULL, &glow);
    SDL_SetTextureColorMod(texGlow, 255, 255, 255);
    SDL_SetTextureAlphaMod(texGlow, 255);
  }

  // Explosive bricks breathe like a lit fuse
  float fuse = 0;

  if (brick->kind == BRICK_EXPLOSIVE)
  {
    fuse = 0.5f + 0.5f * sinf(gameTime * 4.5f + phase);
    float size = (54 + 14 * fuse) * scale;
    SDL_SetTextureColorMod(texGlow, 255, 140, 50);
    SDL_SetTextureAlphaMod(texGlow, (Uint8)((40 + 70 * fuse) * alpha / 255));
    SDL_FRect glow = {
        dst.x + dst.w / 2.0f - size / 2,
        dst.y + dst.h / 2.0f - size / 2,
        size,
        size};
    SDL_RenderTexture(renderer, texGlow, NULL, &glow);
    SDL_SetTextureColorMod(texGlow, 255, 255, 255);
    SDL_SetTextureAlphaMod(texGlow, 255);
  }

  // The same wave runs a faint brightness ripple through the wall
  float lit = 1.0f + 0.10f * wave;

  SDL_SetTextureColorMod(
      tex,
      (Uint8)clamp((int)(brick->tint.r * lit), 0, 255),
      (Uint8)clamp((int)(brick->tint.g * lit), 0, 255),
      (Uint8)clamp((int)(brick->tint.b * lit), 0, 255));
  SDL_SetTextureAlphaMod(tex, alpha);
  SDL_RenderTextureRotated(renderer, tex, NULL, &dst, angle, NULL, SDL_FLIP_NONE);

  // The post-hit white flash and the travelling glint are the same additive
  // pass over the brick, so a brick that is hit mid-sweep costs no extra draw
  float additive = (170.0f * flash + 100.0f * brickShine(brick->pos.x, screenY)) *
                   alpha / 255.0f;

  if (additive >= 1.0f)
  {
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_ADD);
    SDL_SetTextureColorMod(tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(tex, (Uint8)fminf(255.0f, additive));
    SDL_RenderTextureRotated(renderer, tex, NULL, &dst, angle, NULL, SDL_FLIP_NONE);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
  }

  SDL_SetTextureColorMod(tex, 255, 255, 255);
  SDL_SetTextureAlphaMod(tex, 255);

  // Bomb emblem on explosive bricks, brightening with the fuse
  if (brick->kind == BRICK_EXPLOSIVE)
  {
    SDL_SetTextureAlphaMod(texBombEmblem, (Uint8)(alpha * (0.7f + 0.3f * fuse)));
    SDL_RenderTextureRotated(renderer, texBombEmblem, NULL, &dst, angle, NULL, SDL_FLIP_NONE);
    SDL_SetTextureAlphaMod(texBombEmblem, 255);
  }

  // Damage cracks
  if (damage == 1 && brick->maxHp > 1)
  {
    SDL_RenderTextureRotated(renderer, texCrackLight, NULL, &dst, angle, NULL, SDL_FLIP_NONE);
  }
  else if (damage >= 2)
  {
    SDL_RenderTextureRotated(renderer, texCrackHeavy, NULL, &dst, angle, NULL, SDL_FLIP_NONE);
  }

  // Gold catches the light: a star flare winks on and off across the face
  if (brick->kind == BRICK_GOLD)
  {
    float twinkle = sinf(gameTime * 1.9f + phase * 4.0f);

    if (twinkle > 0.6f)
    {
      float k = (twinkle - 0.6f) / 0.4f;
      float size = 30 * k * scale;
      SDL_SetTextureColorMod(texSparkle, 255, 235, 160);
      SDL_SetTextureAlphaMod(texSparkle, (Uint8)(220 * k * alpha / 255));
      SDL_FRect flare = {
          dst.x + dst.w * 0.68f - size / 2,
          dst.y + dst.h * 0.32f - size / 2,
          size,
          size};
      SDL_RenderTextureRotated(renderer, texSparkle, NULL, &flare,
                               gameTime * 35.0f, NULL, SDL_FLIP_NONE);
      SDL_SetTextureColorMod(texSparkle, 255, 255, 255);
      SDL_SetTextureAlphaMod(texSparkle, 255);
    }
  }
}

// ---------------------------------------------------------------------------
// Dying bricks: destroyed bricks spin, shrink and fade instead of vanishing
// ---------------------------------------------------------------------------

#define MAX_DYING_BRICKS 32

typedef struct DyingBrick
{
  Vec2 pos;
  Vec2 vel;
  float rot;
  float rotVel;
  float life;
  float maxLife;
  SDL_Color tint;
  BrickKind kind;
  bool active;
} DyingBrick;

static DyingBrick dyingBricks[MAX_DYING_BRICKS];
static int nextDyingBrick;

void clearDyingBricks(void)
{
  for (int i = 0; i < MAX_DYING_BRICKS; i++)
  {
    dyingBricks[i].active = false;
  }
}

void spawnDyingBrick(const Brick *brick)
{
  DyingBrick *d = &dyingBricks[nextDyingBrick];
  nextDyingBrick = (nextDyingBrick + 1) % MAX_DYING_BRICKS;

  d->pos = brick->pos;
  d->vel = (Vec2){frandRange(-50, 50), frandRange(-170, -90)};
  d->rot = 0;
  d->rotVel = frandRange(-280, 280);
  d->maxLife = d->life = 0.45f;
  d->tint = brick->tint;
  d->kind = brick->kind;
  d->active = true;
}

void updateDyingBricks(void)
{
  for (int i = 0; i < MAX_DYING_BRICKS; i++)
  {
    DyingBrick *d = &dyingBricks[i];

    if (!d->active)
    {
      continue;
    }

    d->life -= (float)dt;

    if (d->life <= 0)
    {
      d->active = false;
      continue;
    }

    d->vel.y += 850.0f * (float)dt;
    d->pos.x += d->vel.x * (float)dt;
    d->pos.y += d->vel.y * (float)dt;
    d->rot += d->rotVel * (float)dt;
  }
}

void drawDyingBricks(void)
{
  for (int i = 0; i < MAX_DYING_BRICKS; i++)
  {
    DyingBrick *d = &dyingBricks[i];

    if (!d->active)
    {
      continue;
    }

    float t = d->life / d->maxLife; // 1 -> 0
    float screenY = d->pos.y - camera.y;

    if (screenY < -BRICK_HEIGHT - 40 || screenY > SCREEN_HEIGHT + 40)
    {
      continue;
    }

    SDL_Texture *tex = textureForKind(d->kind);
    float scale = 0.55f + 0.45f * t;
    float w = BRICK_WIDTH * scale;
    float h = BRICK_HEIGHT * scale;

    SDL_FRect dst = {
        d->pos.x + (BRICK_WIDTH - w) / 2.0f,
        screenY + (BRICK_HEIGHT - h) / 2.0f,
        w,
        h};

    SDL_SetTextureColorMod(tex, d->tint.r, d->tint.g, d->tint.b);
    SDL_SetTextureAlphaMod(tex, (Uint8)(230 * t));
    SDL_RenderTextureRotated(renderer, tex, NULL, &dst, d->rot, NULL, SDL_FLIP_NONE);
    SDL_SetTextureColorMod(tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(tex, 255);
  }
}
