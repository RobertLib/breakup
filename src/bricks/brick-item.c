#include "brick-item.h"
#include "../balls/balls.h"
#include "../lib/audio.h"
#include "../lib/camera.h"
#include "../lib/effects.h"
#include "../lib/particles.h"
#include "../paddle/paddle.h"
#include "../ui/floating-text.h"

#define MAX_ITEMS 12
#define ITEM_FALL_SPEED 170

typedef struct BrickItem
{
  Vec2 pos;
  ItemType type;
  float spin;
  bool active;
} BrickItem;

static BrickItem items[MAX_ITEMS];

// Drop weights (do not need to sum to anything specific)
static const int itemWeights[ITEM_COUNT] = {
    14, // LONG
    12, // SHOOT
    10, // STICKY
    5,  // LIFE
    10, // MULTI
    12, // SLOW
    10, // FIRE
    11, // SHIELD
    16, // SHRINK (the bad one)
};

static const char *itemNames[ITEM_COUNT] = {
    "EXPAND", "LASER", "CATCH", "+1 LIFE", "MULTIBALL",
    "SLOW", "FIREBALL", "BARRIER", "SHRINK!"};

// Both tables are indexed by ItemType, and a missing entry is a zero rather
// than a compile error: a tenth power-up added to the enum would silently drop
// through rollItemType() with a weight of nothing and arrive at a NULL name.
// This is the same guard musicFiles carries in src/lib/audio.c, for the same
// reason - the alternative is finding out by playing.
_Static_assert(SDL_arraysize(itemWeights) == ITEM_COUNT,
               "itemWeights must hold exactly one weight per ItemType");
_Static_assert(SDL_arraysize(itemNames) == ITEM_COUNT,
               "itemNames must hold exactly one label per ItemType");

// Where the capsule actually is, sway included. drawBrickItems() applied the
// sway and updateBrickItems() did not, so the player was catching a capsule up
// to five pixels to one side of the one they could see.
static float itemDrawX(const BrickItem *item)
{
  return item->pos.x + sinf(item->spin * 0.7f) * 5.0f;
}

static ItemType rollItemType(void)
{
  int total = 0;

  for (int i = 0; i < ITEM_COUNT; i++)
  {
    total += itemWeights[i];
  }

  int roll = rand() % total;

  for (int i = 0; i < ITEM_COUNT; i++)
  {
    roll -= itemWeights[i];

    if (roll < 0)
    {
      return (ItemType)i;
    }
  }

  return ITEM_LONG;
}

void resetBrickItems(void)
{
  for (int i = 0; i < MAX_ITEMS; i++)
  {
    items[i].active = false;
  }
}

void trySpawnBrickItem(float x, float y)
{
  for (int i = 0; i < MAX_ITEMS; i++)
  {
    if (!items[i].active)
    {
      items[i] = (BrickItem){{x, y}, rollItemType(), frand() * 6.28f, true};
      return;
    }
  }
}

static void applyItem(ItemType type, float x, float y)
{
  bool good = type != ITEM_SHRINK;

  switch (type)
  {
  case ITEM_LONG:
    changePaddleType(PADDLE_TYPE_LONG);
    break;
  case ITEM_SHOOT:
    changePaddleType(PADDLE_TYPE_SHOOTING);
    break;
  case ITEM_STICKY:
    changePaddleType(PADDLE_TYPE_STICKY);
    break;
  case ITEM_LIFE:
    if (paddle.lives < 9)
    {
      paddle.lives++;
    }
    break;
  case ITEM_MULTI:
    activateAllBalls();
    break;
  case ITEM_SLOW:
    effects.slow = EFFECT_SLOW_DURATION;
    break;
  case ITEM_FIRE:
    effects.fire = EFFECT_FIRE_DURATION;
    break;
  case ITEM_SHIELD:
    effects.shield = EFFECT_SHIELD_DURATION;
    break;
  case ITEM_SHRINK:
    changePaddleType(PADDLE_TYPE_SHORT);
    break;
  default:
    break;
  }

  paddleImpactKick();
  playSfx(good ? SFX_POWERUP_GOOD : SFX_POWERUP_BAD);

  SDL_Color color = good ? (SDL_Color){160, 255, 190, 255}
                         : (SDL_Color){255, 120, 100, 255};

  // The switch above has a `default`, so a value from outside the enum reaches
  // this line rather than being turned away by the compiler - and this line
  // indexes an array with it. Nothing sends one today: rollItemType() is the
  // only source of an ItemType and it answers 0..ITEM_COUNT-1. The check is
  // one comparison, in a function that runs when a capsule is caught, and it
  // is the difference between a wrong power-up and a read off the end of a
  // table of pointers. The cast is what makes it one comparison: a negative
  // value wraps to something enormous and fails the same test.
  const char *name =
      (unsigned)type < (unsigned)ITEM_COUNT ? itemNames[type] : "?";

  spawnFloatingText(x, y - 12, name, color);
  spawnGlowPuff(x, y, color, 70, 0.35f);
}

void updateBrickItems(void)
{
  for (int i = 0; i < MAX_ITEMS; i++)
  {
    BrickItem *item = &items[i];

    if (!item->active)
    {
      continue;
    }

    item->pos.y += ITEM_FALL_SPEED * (float)dt;
    item->spin += (float)dt * 5.0f;

    if (item->pos.y > camera.y + SCREEN_HEIGHT)
    {
      item->active = false;
      continue;
    }

    // A paddle that is dying catches nothing, and falls through the same
    // guard the enemy bullets use. Half of these call changePaddleType(),
    // which would overwrite PADDLE_TYPE_DYING and take the life loss with it:
    // updateTimers() then zeroes lifeLossTimer, respawnOrGameOver() never
    // runs, and updateBalls() starts the whole death over on the next frame -
    // its sound and its screen shake included, once per capsule caught.
    float drawX = itemDrawX(item);

    if (paddle.type != PADDLE_TYPE_DYING &&
        checkCollision(
            drawX,
            item->pos.y,
            GFX_ITEM_W,
            GFX_ITEM_H,
            paddle.pos.x - paddleWidth() / 2,
            paddle.pos.y,
            paddleWidth(),
            PADDLE_HEIGHT))
    {
      item->active = false;
      applyItem(item->type,
                drawX + GFX_ITEM_W / 2.0f,
                item->pos.y);
    }
  }
}

void drawBrickItems(void)
{
  for (int i = 0; i < MAX_ITEMS; i++)
  {
    BrickItem *item = &items[i];

    if (!item->active)
    {
      continue;
    }

    float screenY = item->pos.y - camera.y;

    if (screenY < -GFX_ITEM_H || screenY > SCREEN_HEIGHT + GFX_ITEM_H)
    {
      continue;
    }

    // Gentle sideways sway while falling; itemDrawX() is what the collision
    // above uses too, so the two cannot drift apart again.
    float cx = itemDrawX(item) + GFX_ITEM_W / 2.0f;

    // Pulsing glow in the capsule's color
    SDL_Color col = itemColor(item->type);
    float pulse = 0.7f + 0.3f * sinf(item->spin * 2.2f);
    float glowSize = 52 * pulse;

    SDL_SetTextureColorMod(texGlow, col.r, col.g, col.b);
    SDL_SetTextureAlphaMod(texGlow, (Uint8)(85 * pulse));
    SDL_FRect glow = {
        cx - glowSize / 2,
        screenY + GFX_ITEM_H / 2.0f - glowSize / 2,
        glowSize,
        glowSize};
    SDL_RenderTexture(renderer, texGlow, NULL, &glow);
    SDL_SetTextureColorMod(texGlow, 255, 255, 255);
    SDL_SetTextureAlphaMod(texGlow, 255);

    // Spin: capsule narrows and widens horizontally
    float sx = 0.35f + 0.65f * fabsf(cosf(item->spin));
    float w = GFX_ITEM_W * sx;

    SDL_FRect dst = {
        cx - w / 2.0f,
        screenY,
        w,
        GFX_ITEM_H};
    SDL_RenderTexture(renderer, texItem[item->type], NULL, &dst);
  }
}
