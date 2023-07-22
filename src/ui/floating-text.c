#include "floating-text.h"
#include "../lib/camera.h"

#define MAX_FLOATING_TEXTS 24
#define FLOAT_LIFE 1.0f

// The longest thing anybody spawns is "MULTIBALL"; the rest are point values.
// Anything longer than this still draws correctly - the texture is rendered
// from the caller's string, not from the copy - it just stops being cacheable,
// because the truncated copy can never compare equal to what is looked up.
#define FLOAT_TEXT_MAX 16
#define TEXT_CACHE_SIZE 32

typedef struct FloatingText
{
  Vec2 pos;
  float life;
  SDL_Texture *texture;
  bool active;
} FloatingText;

static FloatingText texts[MAX_FLOATING_TEXTS];
static int nextText;

// Rasterized popups, kept and reused. A FloatingText borrows one of these; the
// cache owns them and destroyFloatingTexts() is where they go.
typedef struct CachedText
{
  char text[FLOAT_TEXT_MAX];
  SDL_Color color;
  SDL_Texture *texture;
} CachedText;

static CachedText cache[TEXT_CACHE_SIZE];
static int nextCacheSlot;

static bool sameColor(SDL_Color a, SDL_Color b)
{
  return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// Popups repeat relentlessly - the same handful of point values, the same nine
// power-up names - and every one of them used to cost a TTF rasterization and a
// GPU upload, once per brick destroyed, with a matching destroy a second later.
// A miss here evicts the oldest entry, which is safe by counting: 32 misses
// means at least 32 spawns, and 24 spawns have already recycled every slot in
// texts[], so nothing on screen can still be pointing at it. The loop that
// follows makes that an invariant rather than an argument.
static SDL_Texture *cachedText(const char *text, SDL_Color color)
{
  for (int i = 0; i < TEXT_CACHE_SIZE; i++)
  {
    if (cache[i].texture != NULL && sameColor(cache[i].color, color) &&
        SDL_strcmp(cache[i].text, text) == 0)
    {
      return cache[i].texture;
    }
  }

  CachedText *slot = &cache[nextCacheSlot];
  nextCacheSlot = (nextCacheSlot + 1) % TEXT_CACHE_SIZE;

  if (slot->texture != NULL)
  {
    for (int i = 0; i < MAX_FLOATING_TEXTS; i++)
    {
      if (texts[i].texture == slot->texture)
      {
        texts[i].active = false;
        texts[i].texture = NULL;
      }
    }

    SDL_DestroyTexture(slot->texture);
  }

  SDL_strlcpy(slot->text, text, sizeof(slot->text));
  slot->color = color;
  slot->texture = renderTextBlended(font16, text, color);

  return slot->texture;
}

void clearFloatingTexts(void)
{
  // Only what is on screen. The textures belong to the cache above and outlive
  // a level change on purpose - that is the whole point of keeping them.
  for (int i = 0; i < MAX_FLOATING_TEXTS; i++)
  {
    texts[i].texture = NULL;
    texts[i].active = false;
  }
}

void destroyFloatingTexts(void)
{
  clearFloatingTexts();

  for (int i = 0; i < TEXT_CACHE_SIZE; i++)
  {
    if (cache[i].texture != NULL)
    {
      SDL_DestroyTexture(cache[i].texture);
      cache[i].texture = NULL;
    }

    cache[i].text[0] = '\0';
  }

  nextCacheSlot = 0;
}

void spawnFloatingText(float x, float y, const char *text, SDL_Color color)
{
  // Before texts[nextText] is touched: a cache miss can deactivate entries in
  // texts[], and picking the slot first would let it deactivate the one just
  // filled in.
  SDL_Texture *texture = cachedText(text, color);

  FloatingText *ft = &texts[nextText];
  nextText = (nextText + 1) % MAX_FLOATING_TEXTS;

  ft->texture = texture;
  ft->pos = (Vec2){x, y};
  ft->life = FLOAT_LIFE;
  ft->active = true;
}

void updateFloatingTexts(void)
{
  for (int i = 0; i < MAX_FLOATING_TEXTS; i++)
  {
    FloatingText *ft = &texts[i];

    if (!ft->active)
    {
      continue;
    }

    ft->life -= (float)dt;
    ft->pos.y -= 38.0f * (float)dt;

    if (ft->life <= 0)
    {
      ft->active = false;
      ft->texture = NULL; // borrowed from the cache; not ours to destroy
    }
  }
}

void drawFloatingTexts(void)
{
  for (int i = 0; i < MAX_FLOATING_TEXTS; i++)
  {
    FloatingText *ft = &texts[i];

    if (!ft->active || ft->texture == NULL)
    {
      continue;
    }

    float t = ft->life / FLOAT_LIFE;
    SDL_FPoint size = getSize(ft->texture);

    SDL_SetTextureAlphaMod(ft->texture, (Uint8)(255 * fminf(1.0f, t * 2.0f)));

    // Quick scale pop right after spawning
    float pop = 1.0f + 0.5f * fmaxf(0.0f, (t - 0.8f) / 0.2f);
    float w = size.x * pop;
    float h = size.y * pop;

    SDL_FRect dst = {
        ft->pos.x - w / 2,
        ft->pos.y - camera.y - (h - size.y) / 2,
        w,
        h};
    SDL_RenderTexture(renderer, ft->texture, NULL, &dst);
  }
}
