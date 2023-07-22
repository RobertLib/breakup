#include "starfield.h"
#include "gfx.h"

#define DUST_STARS 70
#define STARS_PER_LAYER 55
#define MAX_METEORS 3

typedef struct Star
{
  float x, y;
  float size;
  float phase;    // twinkle phase offset
  SDL_Color tint; // subtle per-star color variation
} Star;

static Star dustStars[DUST_STARS];
static Star farStars[STARS_PER_LAYER];
static Star nearStars[STARS_PER_LAYER];

// Shooting stars streak across in screen space
typedef struct Meteor
{
  float x, y;
  float vx, vy;
  float life;
  float maxLife;
  bool active;
} Meteor;

static Meteor meteors[MAX_METEORS];
static float meteorTimer;

static SDL_Color starTint(void)
{
  float roll = frand();

  if (roll < 0.12f)
  {
    return (SDL_Color){255, 210, 170, 255}; // warm
  }
  if (roll < 0.30f)
  {
    return (SDL_Color){170, 200, 255, 255}; // cool blue
  }

  return (SDL_Color){225, 235, 255, 255}; // near white
}

static void initStar(Star *s, float minSize, float maxSize)
{
  *s = (Star){
      frand() * SCREEN_WIDTH,
      frand() * SCREEN_HEIGHT,
      frandRange(minSize, maxSize),
      frand() * 6.28f,
      starTint()};
}

void initializeStarfield(void)
{
  for (int i = 0; i < DUST_STARS; i++)
  {
    initStar(&dustStars[i], 1.2f, 2.2f);
  }

  for (int i = 0; i < STARS_PER_LAYER; i++)
  {
    initStar(&farStars[i], 2.0f, 3.6f);
    initStar(&nearStars[i], 3.0f, 6.5f);
  }

  for (int i = 0; i < MAX_METEORS; i++)
  {
    meteors[i].active = false;
  }

  meteorTimer = frandRange(2.0f, 5.0f);
}

static void drawStarLayer(const Star *stars, int count, float parallax,
                          float scrollY, float baseAlpha, bool sparkles)
{
  for (int i = 0; i < count; i++)
  {
    const Star *s = &stars[i];

    float y = fmodf(s->y + scrollY * parallax, (float)SCREEN_HEIGHT);
    if (y < 0)
    {
      y += SCREEN_HEIGHT;
    }

    float twinkle = 0.65f + 0.35f * sinf(gameTime * 2.2f + s->phase);
    Uint8 a = (Uint8)(baseAlpha * twinkle * 255);

    float size = s->size * (0.9f + 0.1f * twinkle);

    SDL_SetTextureColorMod(texStar, s->tint.r, s->tint.g, s->tint.b);
    SDL_SetTextureAlphaMod(texStar, a);
    SDL_FRect rect = {s->x - size / 2, y - size / 2, size, size};
    SDL_RenderTexture(renderer, texStar, NULL, &rect);

    // The biggest stars get a slowly rotating cross flare
    if (sparkles && s->size > 5.2f)
    {
      float fs = s->size * 3.4f;
      float angle = gameTime * 8.0f + s->phase * 57.0f;

      SDL_SetTextureColorMod(texSparkle, s->tint.r, s->tint.g, s->tint.b);
      SDL_SetTextureAlphaMod(texSparkle, (Uint8)(a * 0.55f));
      SDL_FRect flare = {s->x - fs / 2, y - fs / 2, fs, fs};
      SDL_RenderTextureRotated(renderer, texSparkle, NULL, &flare, angle,
                               NULL, SDL_FLIP_NONE);
    }
  }

  SDL_SetTextureColorMod(texStar, 255, 255, 255);
  SDL_SetTextureAlphaMod(texStar, 255);
  SDL_SetTextureColorMod(texSparkle, 255, 255, 255);
  SDL_SetTextureAlphaMod(texSparkle, 255);
}

// Tiled additive wisp layer drifting over time
static void drawFlowLayer(SDL_Color tint, Uint8 alpha, float tile,
                          float offsetX, float offsetY)
{
  offsetX = fmodf(offsetX, tile);
  if (offsetX > 0)
  {
    offsetX -= tile;
  }

  offsetY = fmodf(offsetY, tile);
  if (offsetY > 0)
  {
    offsetY -= tile;
  }

  SDL_SetTextureColorMod(texFlow, tint.r, tint.g, tint.b);
  SDL_SetTextureAlphaMod(texFlow, alpha);

  for (float y = offsetY; y < SCREEN_HEIGHT; y += tile)
  {
    for (float x = offsetX; x < SCREEN_WIDTH; x += tile)
    {
      SDL_FRect dst = {x, y, tile, tile};
      SDL_RenderTexture(renderer, texFlow, NULL, &dst);
    }
  }

  SDL_SetTextureColorMod(texFlow, 255, 255, 255);
  SDL_SetTextureAlphaMod(texFlow, 255);
}

// The background is presentation, not world, so it runs on realDt: a hit stop
// holds the field still and a shooting star crossing behind it should not stall
// in mid-air. It is also the only thing here that moves while the level-complete
// banner is up, where the world is not being updated at all.
static void updateAndDrawMeteors(void)
{
  meteorTimer -= (float)realDt;

  if (meteorTimer <= 0)
  {
    meteorTimer = frandRange(4.0f, 9.0f);

    for (int i = 0; i < MAX_METEORS; i++)
    {
      if (!meteors[i].active)
      {
        float dir = frand() < 0.5f ? -1.0f : 1.0f;
        meteors[i] = (Meteor){
            dir < 0 ? frandRange(SCREEN_WIDTH * 0.4f, SCREEN_WIDTH + 60.0f)
                    : frandRange(-60.0f, SCREEN_WIDTH * 0.6f),
            frandRange(-40.0f, SCREEN_HEIGHT * 0.35f),
            dir * frandRange(220.0f, 380.0f),
            frandRange(130.0f, 230.0f),
            0,
            frandRange(0.9f, 1.4f),
            true};
        break;
      }
    }
  }

  for (int i = 0; i < MAX_METEORS; i++)
  {
    Meteor *m = &meteors[i];

    if (!m->active)
    {
      continue;
    }

    m->life += (float)realDt;
    m->x += m->vx * (float)realDt;
    m->y += m->vy * (float)realDt;

    if (m->life >= m->maxLife)
    {
      m->active = false;
      continue;
    }

    // Fade in and out over the meteor's life
    float t = m->life / m->maxLife;
    float fade = sinf(t * SDL_PI_F);

    // Trail of shrinking glow dots behind the head
    const int TRAIL = 9;
    for (int j = 0; j < TRAIL; j++)
    {
      float back = j * 0.016f;
      float px = m->x - m->vx * back;
      float py = m->y - m->vy * back;
      float ft = 1.0f - j / (float)TRAIL;
      float size = (14.0f - j) * ft;

      SDL_SetTextureColorMod(texGlow, 200, 220, 255);
      SDL_SetTextureAlphaMod(texGlow, (Uint8)(120 * fade * ft));
      SDL_FRect dst = {px - size / 2, py - size / 2, size, size};
      SDL_RenderTexture(renderer, texGlow, NULL, &dst);
    }

    // Bright head
    SDL_SetTextureColorMod(texStar, 255, 255, 255);
    SDL_SetTextureAlphaMod(texStar, (Uint8)(230 * fade));
    SDL_FRect head = {m->x - 4, m->y - 4, 8, 8};
    SDL_RenderTexture(renderer, texStar, NULL, &head);
  }

  SDL_SetTextureColorMod(texGlow, 255, 255, 255);
  SDL_SetTextureAlphaMod(texGlow, 255);
  SDL_SetTextureColorMod(texStar, 255, 255, 255);
  SDL_SetTextureAlphaMod(texStar, 255);
}

void drawBackground(int worldIndex, float scrollY)
{
  worldIndex = clamp(worldIndex, 0, WORLD_COUNT);
  const WorldTheme *theme = &worldThemes[worldIndex];
  SDL_Texture *nebula = texNebula[worldIndex];

  // Nebula scrolls slowly with the world; texture tiles vertically
  float offset = fmodf(scrollY * 0.15f, (float)SCREEN_HEIGHT);
  if (offset < 0)
  {
    offset += SCREEN_HEIGHT;
  }

  SDL_FRect dst1 = {0, offset - SCREEN_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT};
  SDL_FRect dst2 = {0, offset, SCREEN_WIDTH, SCREEN_HEIGHT};
  SDL_RenderTexture(renderer, nebula, NULL, &dst1);
  SDL_RenderTexture(renderer, nebula, NULL, &dst2);

  // Drifting cosmic wisps: two layers moving at different speeds keep
  // the background alive even when the camera is still
  drawFlowLayer(theme->accent, 34, 560.0f,
                -gameTime * 7.0f, scrollY * 0.22f + gameTime * 4.0f);
  drawFlowLayer(theme->glow, 22, 340.0f,
                gameTime * 11.0f, scrollY * 0.4f - gameTime * 6.0f);

  drawStarLayer(dustStars, DUST_STARS, 0.2f, scrollY, 0.4f, false);
  drawStarLayer(farStars, STARS_PER_LAYER, 0.35f, scrollY, 0.6f, false);
  drawStarLayer(nearStars, STARS_PER_LAYER, 0.65f, scrollY, 0.9f, true);

  updateAndDrawMeteors();
}
