#include "gfx.h"

SDL_Texture *texBrick;
SDL_Texture *texBrickSolid;
SDL_Texture *texBrickGold;
SDL_Texture *texCrystal;
SDL_Texture *texBombEmblem;
SDL_Texture *texCrackLight;
SDL_Texture *texCrackHeavy;

SDL_Texture *texBall;
SDL_Texture *texGlow;
SDL_Texture *texSquare;

SDL_Texture *texStar;
SDL_Texture *texSparkle;
SDL_Texture *texFlow;

static SDL_Texture *texVignette;

SDL_Texture *texPaddle[3];
SDL_Texture *texTurret;

SDL_Texture *texEnemy[ENEMY_TYPE_COUNT];

SDL_Texture *texItem[ITEM_COUNT];

SDL_Texture *texNebula[WORLD_COUNT + 1];

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

SDL_Color hsvColor(float h, float s, float v)
{
  h = fmodf(fmodf(h, 360.0f) + 360.0f, 360.0f);

  float c = v * s;
  float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
  float m = v - c;
  float r = 0, g = 0, b = 0;

  if (h < 60) { r = c; g = x; }
  else if (h < 120) { r = x; g = c; }
  else if (h < 180) { g = c; b = x; }
  else if (h < 240) { g = x; b = c; }
  else if (h < 300) { r = x; b = c; }
  else { r = c; b = x; }

  return (SDL_Color){
      (Uint8)((r + m) * 255),
      (Uint8)((g + m) * 255),
      (Uint8)((b + m) * 255),
      255};
}

static SDL_Surface *createSurface(int w, int h)
{
  SDL_Surface *s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);

  if (s == NULL)
  {
    fprintf(stderr, "Unable to create surface! SDL Error: %s\n", SDL_GetError());
    exit(1);
  }

  return s;
}

static void put(SDL_Surface *s, int x, int y, int r, int g, int b, int a)
{
  if (x < 0 || x >= s->w || y < 0 || y >= s->h)
  {
    return;
  }

  Uint8 *p = (Uint8 *)s->pixels + y * s->pitch + x * 4;
  p[0] = (Uint8)clamp(r, 0, 255);
  p[1] = (Uint8)clamp(g, 0, 255);
  p[2] = (Uint8)clamp(b, 0, 255);
  p[3] = (Uint8)clamp(a, 0, 255);
}

// Blend a pixel over what's already there (straight alpha)
static void blend(SDL_Surface *s, int x, int y, int r, int g, int b, int a)
{
  if (x < 0 || x >= s->w || y < 0 || y >= s->h || a <= 0)
  {
    return;
  }

  Uint8 *p = (Uint8 *)s->pixels + y * s->pitch + x * 4;
  float af = clamp(a, 0, 255) / 255.0f;
  p[0] = (Uint8)(p[0] * (1 - af) + clamp(r, 0, 255) * af);
  p[1] = (Uint8)(p[1] * (1 - af) + clamp(g, 0, 255) * af);
  p[2] = (Uint8)(p[2] * (1 - af) + clamp(b, 0, 255) * af);
  p[3] = (Uint8)fmaxf(p[3], (float)clamp(a, 0, 255));
}

// Signed distance to a rounded box centered at origin (half extents bx, by, radius r)
static float sdRoundBox(float px, float py, float bx, float by, float r)
{
  float qx = fabsf(px) - (bx - r);
  float qy = fabsf(py) - (by - r);
  float ox = fmaxf(qx, 0.0f);
  float oy = fmaxf(qy, 0.0f);
  return sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0.0f) - r;
}

// Anti-aliased coverage from a signed distance in logical units,
// with a transition one *physical* texture pixel wide (for GFX_SS surfaces)
static float covS(float d)
{
  return clamp(0.5f - d * GFX_SS, 0.0f, 1.0f);
}

// Coverage for 1x surfaces (one-logical-pixel transition)
static float cov(float d)
{
  return clamp(0.5f - d, 0.0f, 1.0f);
}

// Anti-aliased solid disc at logical coords, blended onto a GFX_SS surface
static void plotDisc(SDL_Surface *s, float cx, float cy, float r,
                     int cr, int cg, int cb, int ca)
{
  int x0 = (int)((cx - r - 1.0f) * GFX_SS);
  int x1 = (int)((cx + r + 1.0f) * GFX_SS) + 1;
  int y0 = (int)((cy - r - 1.0f) * GFX_SS);
  int y1 = (int)((cy + r + 1.0f) * GFX_SS) + 1;

  for (int y = y0; y <= y1; y++)
  {
    for (int x = x0; x <= x1; x++)
    {
      float fx = (x + 0.5f) / GFX_SS - cx;
      float fy = (y + 0.5f) / GFX_SS - cy;
      float d = sqrtf(fx * fx + fy * fy) - r;
      float a = covS(d);

      if (a > 0)
      {
        blend(s, x, y, cr, cg, cb, (int)(a * ca));
      }
    }
  }
}

static SDL_Texture *textureFrom(SDL_Surface *s, bool additive)
{
  SDL_Texture *t = SDL_CreateTextureFromSurface(renderer, s);

  if (t == NULL)
  {
    fprintf(stderr, "Unable to create texture! SDL Error: %s\n", SDL_GetError());
    exit(1);
  }

  SDL_SetTextureBlendMode(t, additive ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
  SDL_DestroySurface(s);

  return t;
}

// Deterministic PRNG so generated art is identical every run
static unsigned int rngState;

static void seedRng(unsigned int seed)
{
  rngState = seed ? seed : 1;
}

static unsigned int rngNext(void)
{
  rngState ^= rngState << 13;
  rngState ^= rngState >> 17;
  rngState ^= rngState << 5;
  return rngState;
}

static float rngFloat(void)
{
  return (rngNext() & 0xffffff) / 16777216.0f;
}

// ---------------------------------------------------------------------------
// Bricks
// ---------------------------------------------------------------------------

static SDL_Texture *makeBrick(void)
{
  const int W = GFX_BRICK_W * GFX_SS;
  const int H = GFX_BRICK_H * GFX_SS;
  SDL_Surface *s = createSurface(W, H);

  for (int y = 0; y < H; y++)
  {
    for (int x = 0; x < W; x++)
    {
      float fx = (x + 0.5f) / GFX_SS;
      float fy = (y + 0.5f) / GFX_SS;
      float px = fx - GFX_BRICK_W / 2.0f;
      float py = fy - GFX_BRICK_H / 2.0f;
      float d = sdRoundBox(px, py, 24.0f, 9.0f, 5.0f);
      float a = covS(d);

      if (a <= 0)
      {
        put(s, x, y, 0, 0, 0, 0);
        continue;
      }

      // Vertical shading: light top, darker bottom
      float lum = 1.0f - 0.35f * (fy / GFX_BRICK_H);
      // Glossy highlight band near the top (smooth falloff)
      float gd = fy - 3.4f;
      lum += 0.26f * expf(-gd * gd / 3.4f);
      // Smooth darker rim just inside the border
      float rim = clamp((d + 2.4f) / 2.4f, 0.0f, 1.0f);
      lum *= 1.0f - 0.30f * rim * rim;

      int v = (int)(clamp(lum, 0.0f, 1.3f) * 235.0f);
      put(s, x, y, v, v, v, (int)(a * 255));
    }
  }

  return textureFrom(s, false);
}

static SDL_Texture *makeBrickSolid(void)
{
  const int W = GFX_BRICK_W * GFX_SS;
  const int H = GFX_BRICK_H * GFX_SS;
  SDL_Surface *s = createSurface(W, H);
  seedRng(0xC0FFEE);

  for (int y = 0; y < H; y++)
  {
    for (int x = 0; x < W; x++)
    {
      float fx = (x + 0.5f) / GFX_SS;
      float fy = (y + 0.5f) / GFX_SS;
      float px = fx - GFX_BRICK_W / 2.0f;
      float py = fy - GFX_BRICK_H / 2.0f;
      float d = sdRoundBox(px, py, 24.5f, 9.5f, 3.0f);
      float a = covS(d);

      if (a <= 0)
      {
        put(s, x, y, 0, 0, 0, 0);
        continue;
      }

      // Brushed metal: horizontal banding plus a touch of noise
      float lum = 0.62f + 0.07f * sinf(fy * 1.9f) + (rngFloat() - 0.5f) * 0.05f;
      lum -= 0.12f * (fy / GFX_BRICK_H);

      float rim = clamp((d + 1.8f) / 1.8f, 0.0f, 1.0f);
      lum *= 1.0f - 0.5f * rim; // strong dark border reads as "unbreakable"

      int base = (int)(clamp(lum, 0.0f, 1.0f) * 255.0f);
      put(s, x, y, (int)(base * 0.82f), (int)(base * 0.88f), base, (int)(a * 255));
    }
  }

  // Corner rivets
  const float rx[4] = {7, 43, 7, 43};
  const float ry[4] = {6, 6, 14, 14};
  for (int i = 0; i < 4; i++)
  {
    plotDisc(s, rx[i], ry[i], 2.4f, 118, 124, 138, 255);
    plotDisc(s, rx[i] - 0.5f, ry[i] - 0.5f, 1.1f, 232, 236, 248, 255);
  }

  return textureFrom(s, false);
}

static SDL_Texture *makeBrickGold(void)
{
  const int W = GFX_BRICK_W * GFX_SS;
  const int H = GFX_BRICK_H * GFX_SS;
  SDL_Surface *s = createSurface(W, H);

  for (int y = 0; y < H; y++)
  {
    for (int x = 0; x < W; x++)
    {
      float fx = (x + 0.5f) / GFX_SS;
      float fy = (y + 0.5f) / GFX_SS;
      float px = fx - GFX_BRICK_W / 2.0f;
      float py = fy - GFX_BRICK_H / 2.0f;
      float d = sdRoundBox(px, py, 24.0f, 9.0f, 5.0f);
      float a = covS(d);

      if (a <= 0)
      {
        put(s, x, y, 0, 0, 0, 0);
        continue;
      }

      float lum = 0.9f - 0.3f * (fy / GFX_BRICK_H);

      // Diagonal shine stripes with anti-aliased edges
      float t = fmodf(fx + fy, 18.0f);
      lum += 0.35f * clamp(2.0f - fabsf(t - 2.0f), 0.0f, 1.0f);

      float gd = fy - 3.0f;
      lum += 0.16f * expf(-gd * gd / 2.6f);

      float rim = clamp((d + 2.2f) / 2.2f, 0.0f, 1.0f);
      lum *= 1.0f - 0.38f * rim;

      lum = clamp(lum, 0.0f, 1.35f);
      put(s, x, y,
          (int)(lum * 240),
          (int)(lum * 188),
          (int)(lum * 60),
          (int)(a * 255));
    }
  }

  return textureFrom(s, false);
}

static SDL_Texture *makeCrystal(void)
{
  const int W = GFX_BRICK_W * GFX_SS;
  const int H = GFX_BRICK_H * GFX_SS;
  SDL_Surface *s = createSurface(W, H);

  for (int y = 0; y < H; y++)
  {
    for (int x = 0; x < W; x++)
    {
      float fx = (x + 0.5f) / GFX_SS;
      float fy = (y + 0.5f) / GFX_SS;
      float px = fx - GFX_BRICK_W / 2.0f;
      float py = fy - GFX_BRICK_H / 2.0f;

      // Diamond shape
      float d = (fabsf(px) / 22.0f + fabsf(py) / 9.0f - 1.0f) * 7.0f;
      float a = covS(d);

      if (a <= 0)
      {
        put(s, x, y, 0, 0, 0, 0);
        continue;
      }

      // Faceted shading: each quadrant catches light differently
      float lum = 0.55f;
      if (py < 0) lum += 0.25f;
      if (px < 0) lum += 0.10f;

      // Bright core
      float core = 1.0f - (fabsf(px) / 22.0f + fabsf(py) / 9.0f);
      lum += core * core * 0.9f;

      // Facet edge lines (anti-aliased)
      lum += 0.2f * covS(fabsf(px) - 0.8f);
      lum += 0.2f * covS(fabsf(py) - 0.8f);

      float rim = clamp((d + 1.8f) / 1.8f, 0.0f, 1.0f);
      lum *= 1.0f - 0.3f * rim;

      int v = (int)(clamp(lum, 0.0f, 1.4f) * 200.0f);
      put(s, x, y, v, v, v, (int)(a * 255));
    }
  }

  return textureFrom(s, false);
}

static SDL_Texture *makeBombEmblem(void)
{
  const int W = GFX_BRICK_W * GFX_SS;
  const int H = GFX_BRICK_H * GFX_SS;
  SDL_Surface *s = createSurface(W, H);

  for (int y = 0; y < H; y++)
  {
    for (int x = 0; x < W; x++)
    {
      put(s, x, y, 0, 0, 0, 0);
    }
  }

  // Bomb body with a specular dot
  const float cx = 25.0f, cy = 11.5f, r = 5.5f;

  for (int y = 0; y < H; y++)
  {
    for (int x = 0; x < W; x++)
    {
      float fx = (x + 0.5f) / GFX_SS;
      float fy = (y + 0.5f) / GFX_SS;
      float dx = fx - cx;
      float dy = fy - cy;
      float d = sqrtf(dx * dx + dy * dy) - r;
      float a = covS(d);

      if (a > 0)
      {
        float hd = sqrtf((dx + 2) * (dx + 2) + (dy + 2) * (dy + 2));
        float spec = clamp(1.0f - hd / 2.0f, 0.0f, 1.0f);
        int v = (int)(26 + spec * 130);
        put(s, x, y, v, v, v + 6, (int)(a * 235));
      }
    }
  }

  // Fuse
  for (int i = 0; i < 10; i++)
  {
    float t = i / 9.0f;
    plotDisc(s, 27.0f + t * 5.0f, 5.5f - t * 2.5f, 0.7f, 190, 150, 80, 235);
  }
  // Spark
  plotDisc(s, 32.6f, 2.4f, 1.3f, 255, 210, 90, 255);
  plotDisc(s, 32.6f, 2.4f, 0.7f, 255, 250, 180, 255);

  return textureFrom(s, false);
}

static SDL_Texture *makeCracks(int numCracks, unsigned int seed)
{
  const int W = GFX_BRICK_W * GFX_SS;
  const int H = GFX_BRICK_H * GFX_SS;
  SDL_Surface *s = createSurface(W, H);
  seedRng(seed);

  for (int y = 0; y < H; y++)
  {
    for (int x = 0; x < W; x++)
    {
      put(s, x, y, 0, 0, 0, 0);
    }
  }

  for (int c = 0; c < numCracks; c++)
  {
    // Start on a random edge, wander toward the middle
    float x = rngFloat() * GFX_BRICK_W;
    float y = (rngNext() & 1) ? 1.0f : GFX_BRICK_H - 2.0f;

    if (rngFloat() < 0.35f)
    {
      x = (rngNext() & 1) ? 1.0f : GFX_BRICK_W - 2.0f;
      y = rngFloat() * GFX_BRICK_H;
    }

    float tx = GFX_BRICK_W / 2.0f + (rngFloat() - 0.5f) * 24.0f;
    float ty = GFX_BRICK_H / 2.0f + (rngFloat() - 0.5f) * 8.0f;

    int steps = 16 + (int)(rngFloat() * 10);
    for (int i = 0; i < steps; i++)
    {
      float t = i / (float)steps;
      float px = lerp(x, tx, t) + (rngFloat() - 0.5f) * 3.0f;
      float py = lerp(y, ty, t) + (rngFloat() - 0.5f) * 3.0f;

      plotDisc(s, px, py, 0.55f, 10, 8, 14, 175);
    }
  }

  return textureFrom(s, false);
}

// ---------------------------------------------------------------------------
// Ball, glow, particles
// ---------------------------------------------------------------------------

static SDL_Texture *makeBall(void)
{
  const int SIZE = GFX_BALL_SIZE * GFX_SS;
  SDL_Surface *s = createSurface(SIZE, SIZE);
  const float c = GFX_BALL_SIZE / 2.0f;

  for (int y = 0; y < SIZE; y++)
  {
    for (int x = 0; x < SIZE; x++)
    {
      float px = (x + 0.5f) / GFX_SS - c;
      float py = (y + 0.5f) / GFX_SS - c;
      float dist = sqrtf(px * px + py * py);
      float d = dist - (c - 1.0f);
      float a = covS(d);

      if (a <= 0)
      {
        put(s, x, y, 0, 0, 0, 0);
        continue;
      }

      // Sphere shading with an off-center specular highlight
      float hx = px + 3.2f;
      float hy = py + 3.2f;
      float hd = sqrtf(hx * hx + hy * hy);
      float lum = 0.55f + fmaxf(0.0f, 1.0f - hd / 11.0f) * 0.75f;

      // Hot specular core
      lum += 0.35f * fmaxf(0.0f, 1.0f - hd / 2.6f);

      float rim = clamp((d + 2.2f) / 2.2f, 0.0f, 1.0f);
      lum *= 1.0f - 0.25f * rim;

      int v = (int)(clamp(lum, 0.0f, 1.3f) * 220.0f);
      put(s, x, y, v, v, v, (int)(a * 255));
    }
  }

  return textureFrom(s, false);
}

static SDL_Texture *makeGlow(void)
{
  SDL_Surface *s = createSurface(GFX_GLOW_SIZE, GFX_GLOW_SIZE);
  const float c = GFX_GLOW_SIZE / 2.0f;

  for (int y = 0; y < GFX_GLOW_SIZE; y++)
  {
    for (int x = 0; x < GFX_GLOW_SIZE; x++)
    {
      float px = (x - c + 0.5f) / c;
      float py = (y - c + 0.5f) / c;
      float r = sqrtf(px * px + py * py);
      float a = fmaxf(0.0f, 1.0f - r);
      a = powf(a, 2.2f);
      put(s, x, y, 255, 255, 255, (int)(a * 255));
    }
  }

  return textureFrom(s, true);
}

// Rounded, anti-aliased shard so rotating particles keep smooth edges
static SDL_Texture *makeSquare(void)
{
  const int SIZE = 24;
  SDL_Surface *s = createSurface(SIZE, SIZE);
  const float half = SIZE / 2.0f;

  for (int y = 0; y < SIZE; y++)
  {
    for (int x = 0; x < SIZE; x++)
    {
      float px = x - half + 0.5f;
      float py = y - half + 0.5f;
      float d = sdRoundBox(px, py, half - 1.5f, half - 1.5f, 3.0f);
      float a = cov(d);
      put(s, x, y, 255, 255, 255, (int)(a * 255));
    }
  }

  return textureFrom(s, false);
}

// ---------------------------------------------------------------------------
// Background elements
// ---------------------------------------------------------------------------

static SDL_Texture *makeStar(void)
{
  const int SIZE = 24;
  SDL_Surface *s = createSurface(SIZE, SIZE);
  const float c = SIZE / 2.0f;

  for (int y = 0; y < SIZE; y++)
  {
    for (int x = 0; x < SIZE; x++)
    {
      float px = (x - c + 0.5f) / c;
      float py = (y - c + 0.5f) / c;
      float r = sqrtf(px * px + py * py);
      float t = fmaxf(0.0f, 1.0f - r);
      float a = powf(t, 3.0f) * 0.5f + powf(t, 10.0f) * 0.6f;
      put(s, x, y, 255, 255, 255, (int)(clamp(a, 0.0f, 1.0f) * 255));
    }
  }

  return textureFrom(s, true);
}

static SDL_Texture *makeSparkle(void)
{
  const int SIZE = 64;
  SDL_Surface *s = createSurface(SIZE, SIZE);
  const float c = SIZE / 2.0f;

  for (int y = 0; y < SIZE; y++)
  {
    for (int x = 0; x < SIZE; x++)
    {
      float px = x - c + 0.5f;
      float py = y - c + 0.5f;

      // Two thin flare arms plus a hot core
      float armH = powf(clamp(1.0f - (fabsf(px) + 6.0f * fabsf(py)) / c, 0.0f, 1.0f), 2.2f);
      float armV = powf(clamp(1.0f - (fabsf(py) + 6.0f * fabsf(px)) / c, 0.0f, 1.0f), 2.2f);
      float r = sqrtf(px * px + py * py);
      float core = powf(fmaxf(0.0f, 1.0f - r / 7.0f), 2.0f);

      float a = clamp(armH + armV + core, 0.0f, 1.0f);
      put(s, x, y, 255, 255, 255, (int)(a * 255));
    }
  }

  return textureFrom(s, true);
}

static float hash2(unsigned int seed, int x, int y)
{
  unsigned int h = seed;
  h ^= (unsigned int)x * 374761393u;
  h ^= (unsigned int)y * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  h ^= h >> 16;
  return (h & 0xffffff) / 16777216.0f;
}

static float smoothT(float t)
{
  return t * t * (3.0f - 2.0f * t);
}

// Tileable value noise over a w x h texture (cell must divide w and h)
static float noiseOctave(unsigned int seed, float x, float y, int cell, int w, int h)
{
  int gw = w / cell;
  int gh = h / cell;

  int gx = (int)(x / cell);
  int gy = (int)(y / cell);
  float fx = smoothT(x / cell - gx);
  float fy = smoothT(y / cell - gy);

  float v00 = hash2(seed, gx % gw, gy % gh);
  float v10 = hash2(seed, (gx + 1) % gw, gy % gh);
  float v01 = hash2(seed, gx % gw, (gy + 1) % gh);
  float v11 = hash2(seed, (gx + 1) % gw, (gy + 1) % gh);

  return lerp(lerp(v00, v10, fx), lerp(v01, v11, fx), fy);
}

// Tileable additive wisp layer, tinted at draw time
static SDL_Texture *makeFlow(void)
{
  const int W = 256;
  const int H = 256;
  SDL_Surface *s = createSurface(W, H);

  for (int y = 0; y < H; y++)
  {
    for (int x = 0; x < W; x++)
    {
      float n = 0.5f * noiseOctave(0xF10A, (float)x, (float)y, 64, W, H) +
                0.3f * noiseOctave(0xF10B, (float)x, (float)y, 32, W, H) +
                0.2f * noiseOctave(0xF10C, (float)x, (float)y, 16, W, H);

      float a = powf(n, 2.6f);
      put(s, x, y, 255, 255, 255, (int)(clamp(a, 0.0f, 1.0f) * 255));
    }
  }

  return textureFrom(s, true);
}

static SDL_Texture *makeVignette(void)
{
  const int W = 320;
  const int H = 240;
  SDL_Surface *s = createSurface(W, H);

  for (int y = 0; y < H; y++)
  {
    for (int x = 0; x < W; x++)
    {
      float nx = (x / (W - 1.0f)) * 2.0f - 1.0f;
      float ny = (y / (H - 1.0f)) * 2.0f - 1.0f;
      float r = sqrtf(nx * nx + ny * ny) / 1.4142f;

      float v = clamp((r - 0.52f) / 0.48f, 0.0f, 1.0f);
      float a = powf(v, 1.9f) * 0.5f;
      put(s, x, y, 4, 6, 16, (int)(a * 255));
    }
  }

  return textureFrom(s, false);
}

void drawVignette(void)
{
  SDL_FRect full = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
  SDL_RenderTexture(renderer, texVignette, NULL, &full);
}

// ---------------------------------------------------------------------------
// Paddle
// ---------------------------------------------------------------------------

static SDL_Texture *makePaddle(int w)
{
  const int W = w * GFX_SS;
  const int H = GFX_PADDLE_H * GFX_SS;
  SDL_Surface *s = createSurface(W, H);

  for (int y = 0; y < H; y++)
  {
    for (int x = 0; x < W; x++)
    {
      float fx = (x + 0.5f) / GFX_SS;
      float fy = (y + 0.5f) / GFX_SS;
      float px = fx - w / 2.0f;
      float py = fy - GFX_PADDLE_H / 2.0f;
      float d = sdRoundBox(px, py, w / 2.0f - 1.0f, 9.0f, 8.5f);
      float a = covS(d);

      if (a <= 0)
      {
        put(s, x, y, 0, 0, 0, 0);
        continue;
      }

      // Metallic body
      float lum = 0.95f - 0.42f * (fy / GFX_PADDLE_H);

      // Neon core stripe through the middle (smooth falloff)
      float cd = fy - 10.0f;
      lum += 0.55f * expf(-cd * cd / 2.4f);

      // Bright end caps
      float capT = clamp((fabsf(px) - (w / 2.0f - 9.0f)) / 5.0f, 0.0f, 1.0f);
      lum += 0.18f * capT;

      // Top gloss
      float gd = fy - 2.4f;
      lum += 0.2f * expf(-gd * gd / 2.2f);

      float rim = clamp((d + 1.9f) / 1.9f, 0.0f, 1.0f);
      lum *= 1.0f - 0.4f * rim;

      int v = (int)(clamp(lum, 0.0f, 1.4f) * 200.0f);
      put(s, x, y, v, v, v, (int)(a * 255));
    }
  }

  return textureFrom(s, false);
}

static SDL_Texture *makeTurret(void)
{
  const int LW = 12, LH = 14;
  const int W = LW * GFX_SS, H = LH * GFX_SS;
  SDL_Surface *s = createSurface(W, H);

  for (int y = 0; y < H; y++)
  {
    for (int x = 0; x < W; x++)
    {
      float fx = (x + 0.5f) / GFX_SS;
      float fy = (y + 0.5f) / GFX_SS;

      // Barrel: rounded column in the middle
      float dBarrel = sdRoundBox(fx - 6.0f, fy - 3.8f, 2.0f, 3.6f, 1.0f);

      // Base: widens toward the paddle
      float spread = clamp((fy - 7.0f) * 0.5f, 0.0f, 3.0f);
      float dBase = sdRoundBox(fx - 6.0f, fy - 10.5f, 3.0f + spread, 3.4f, 1.2f);

      float d = fminf(dBarrel, dBase);
      float a = covS(d);

      if (a <= 0)
      {
        put(s, x, y, 0, 0, 0, 0);
        continue;
      }

      float rr, gg, bb;

      if (dBarrel < dBase)
      {
        // Warm metal barrel with a bright center line
        float v = 120.0f + 90.0f * covS(fabsf(fx - 6.0f) - 1.0f);
        rr = v; gg = v * 0.62f; bb = v * 0.55f;

        // Hot muzzle tip
        if (fy < 1.6f)
        {
          rr = 255; gg = 185; bb = 125;
        }
      }
      else
      {
        float v = 120.0f - (fy - 7.0f) * 6.0f;
        rr = v; gg = v * 0.78f; bb = v * 0.78f;
      }

      float rim = clamp((d + 1.2f) / 1.2f, 0.0f, 1.0f);
      rr *= 1.0f - 0.35f * rim;
      gg *= 1.0f - 0.35f * rim;
      bb *= 1.0f - 0.35f * rim;

      put(s, x, y, (int)rr, (int)gg, (int)bb, (int)(a * 255));
    }
  }

  return textureFrom(s, false);
}

// ---------------------------------------------------------------------------
// Enemies
// ---------------------------------------------------------------------------

static SDL_Texture *makeSpikyOrb(int size, float coreR, float spikeLen, int spikes,
                                 SDL_Color color)
{
  const int S = size * GFX_SS;
  SDL_Surface *s = createSurface(S, S);
  const float c = size / 2.0f;

  for (int y = 0; y < S; y++)
  {
    for (int x = 0; x < S; x++)
    {
      float px = (x + 0.5f) / GFX_SS - c;
      float py = (y + 0.5f) / GFX_SS - c;
      float dist = sqrtf(px * px + py * py);
      float ang = atan2f(py, px);

      float spike = fmaxf(0.0f, cosf(ang * spikes));
      float r = coreR + spikeLen * spike * spike * spike;
      float d = dist - r;
      float a = covS(d);

      if (a <= 0)
      {
        put(s, x, y, 0, 0, 0, 0);
        continue;
      }

      // Hot center fading to the edge color
      float coreT = fmaxf(0.0f, 1.0f - dist / (coreR * 0.85f));
      float rr = lerp(color.r, 255, coreT);
      float gg = lerp(color.g, 255, coreT);
      float bb = lerp(color.b, 255, coreT);

      float rim = clamp((d + 1.8f) / 1.8f, 0.0f, 1.0f);
      rr *= 1.0f - 0.45f * rim;
      gg *= 1.0f - 0.45f * rim;
      bb *= 1.0f - 0.45f * rim;

      put(s, x, y, (int)rr, (int)gg, (int)bb, (int)(a * 255));
    }
  }

  return textureFrom(s, false);
}

static float sdSegmentDist(float px, float py, float ax, float ay, float bx, float by)
{
  float abx = bx - ax, aby = by - ay;
  float apx = px - ax, apy = py - ay;
  float t = clamp((apx * abx + apy * aby) / (abx * abx + aby * aby), 0.0f, 1.0f);
  float dx = apx - abx * t, dy = apy - aby * t;
  return sqrtf(dx * dx + dy * dy);
}

// Signed distance to a triangle (positive outside)
static float sdTriangle(float px, float py,
                        float ax, float ay, float bx, float by, float cx, float cy)
{
  float d = fminf(fminf(
                      sdSegmentDist(px, py, ax, ay, bx, by),
                      sdSegmentDist(px, py, bx, by, cx, cy)),
                  sdSegmentDist(px, py, cx, cy, ax, ay));

  // Inside test via cross products
  float c1 = (bx - ax) * (py - ay) - (by - ay) * (px - ax);
  float c2 = (cx - bx) * (py - by) - (cy - by) * (px - bx);
  float c3 = (ax - cx) * (py - cy) - (ay - cy) * (px - cx);
  bool inside = (c1 >= 0 && c2 >= 0 && c3 >= 0) || (c1 <= 0 && c2 <= 0 && c3 <= 0);

  return inside ? -d : d;
}

static SDL_Texture *makeDiver(void)
{
  const int S = GFX_ENEMY_SIZE * GFX_SS;
  SDL_Surface *s = createSurface(S, S);

  for (int y = 0; y < S; y++)
  {
    for (int x = 0; x < S; x++)
    {
      float px = (x + 0.5f) / GFX_SS;
      float py = (y + 0.5f) / GFX_SS;
      float d = sdTriangle(px, py, 16, 30, 3, 6, 29, 6);
      float a = covS(d);

      if (a <= 0)
      {
        put(s, x, y, 0, 0, 0, 0);
        continue;
      }

      // Orange dart with a yellow center stripe
      float rr = 255, gg = 130, bb = 40;
      float stripe = covS(fabsf(px - 16.0f) - 3.0f);
      rr = lerp(rr, 255, stripe);
      gg = lerp(gg, 225, stripe);
      bb = lerp(bb, 100, stripe);

      // Darker top fin area
      if (py < 10)
      {
        rr *= 0.75f; gg *= 0.75f; bb *= 0.75f;
      }

      float rim = clamp((d + 1.8f) / 1.8f, 0.0f, 1.0f);
      rr *= 1.0f - 0.45f * rim;
      gg *= 1.0f - 0.45f * rim;
      bb *= 1.0f - 0.45f * rim;

      put(s, x, y, (int)rr, (int)gg, (int)bb, (int)(a * 255));
    }
  }

  // Eye dots
  plotDisc(s, 12.5f, 12.5f, 1.0f, 20, 20, 30, 255);
  plotDisc(s, 19.5f, 12.5f, 1.0f, 20, 20, 30, 255);

  return textureFrom(s, false);
}

static SDL_Texture *makeSplitter(void)
{
  const int S = GFX_ENEMY_SIZE * GFX_SS;
  SDL_Surface *s = createSurface(S, S);

  for (int y = 0; y < S; y++)
  {
    for (int x = 0; x < S; x++)
    {
      float px = (x + 0.5f) / GFX_SS;
      float py = (y + 0.5f) / GFX_SS;

      float d1 = sqrtf((px - 10) * (px - 10) + (py - 16) * (py - 16)) - 8.5f;
      float d2 = sqrtf((px - 22) * (px - 22) + (py - 16) * (py - 16)) - 8.5f;
      float d = fminf(d1, d2);
      float a = covS(d);

      if (a <= 0)
      {
        put(s, x, y, 0, 0, 0, 0);
        continue;
      }

      float rr = 140, gg = 245, bb = 90;
      // Brighter where the two blobs overlap
      if (d1 < 0.5f && d2 < 0.5f)
      {
        rr = 210; gg = 255; bb = 160;
      }
      // Top light
      if (py < 13)
      {
        rr *= 1.12f; gg *= 1.12f; bb *= 1.12f;
      }

      float rim = clamp((d + 1.8f) / 1.8f, 0.0f, 1.0f);
      rr *= 1.0f - 0.45f * rim;
      gg *= 1.0f - 0.45f * rim;
      bb *= 1.0f - 0.45f * rim;

      put(s, x, y, (int)rr, (int)gg, (int)bb, (int)(a * 255));
    }
  }

  // Eyes
  plotDisc(s, 10.0f, 14.5f, 1.1f, 15, 30, 10, 255);
  plotDisc(s, 22.0f, 14.5f, 1.1f, 15, 30, 10, 255);

  return textureFrom(s, false);
}

static SDL_Texture *makeShooter(void)
{
  const int S = GFX_ENEMY_SIZE * GFX_SS;
  SDL_Surface *s = createSurface(S, S);

  for (int y = 0; y < S; y++)
  {
    for (int x = 0; x < S; x++)
    {
      float px = (x + 0.5f) / GFX_SS - 16.0f;
      float py = (y + 0.5f) / GFX_SS;

      // Saucer body: squashed ellipse
      float ex = px / 14.5f;
      float ey = (py - 20.0f) / 6.0f;
      float dBody = (sqrtf(ex * ex + ey * ey) - 1.0f) * 5.0f;

      // Glass dome on top
      float dx = px / 6.5f;
      float dy = (py - 15.0f) / 6.5f;
      float dDome = (sqrtf(dx * dx + dy * dy) - 1.0f) * 6.0f;
      if (py > 17.0f)
      {
        dDome = 100.0f; // dome only above the body line
      }

      float d = fminf(dBody, dDome);
      float a = covS(d);

      if (a <= 0)
      {
        put(s, x, y, 0, 0, 0, 0);
        continue;
      }

      float rr, gg, bb;
      if (dDome < dBody)
      {
        // Glass dome
        rr = 130; gg = 200; bb = 255;
        float coreD = sqrtf(px * px + (py - 14.5f) * (py - 14.5f));
        float coreT = covS(coreD - 2.4f);
        rr = lerp(rr, 255, coreT); // menacing red core
        gg = lerp(gg, 70, coreT);
        bb = lerp(bb, 70, coreT);
      }
      else
      {
        // Metal body
        float lum = 0.85f - (py - 15.0f) * 0.035f;
        rr = 150 * lum; gg = 168 * lum; bb = 205 * lum;
        // Rim lights
        if (py > 20.0f)
        {
          float rl = covS(fabsf(fmodf(px + 20.0f, 8.0f) - 4.0f) - 1.0f);
          rr = lerp(rr, 255, rl);
          gg = lerp(gg, 220, rl);
          bb = lerp(bb, 110, rl);
        }
      }

      float rim = clamp((d + 1.6f) / 1.6f, 0.0f, 1.0f);
      rr *= 1.0f - 0.45f * rim;
      gg *= 1.0f - 0.45f * rim;
      bb *= 1.0f - 0.45f * rim;

      put(s, x, y, (int)rr, (int)gg, (int)bb, (int)(a * 255));
    }
  }

  return textureFrom(s, false);
}

// ---------------------------------------------------------------------------
// Power-up capsules
// ---------------------------------------------------------------------------

static const SDL_Color itemColors[ITEM_COUNT] = {
    {60, 220, 120, 255},  // LONG
    {255, 90, 80, 255},   // SHOOT
    {255, 210, 70, 255},  // STICKY
    {255, 120, 170, 255}, // LIFE
    {80, 200, 255, 255},  // MULTI
    {100, 140, 255, 255}, // SLOW
    {255, 150, 50, 255},  // FIRE
    {170, 110, 255, 255}, // SHIELD
    {180, 60, 60, 255},   // SHRINK
};

static const char *itemLetters[ITEM_COUNT] = {
    "E", "L", "C", "P", "D", "S", "F", "B", "!"};

SDL_Color itemColor(ItemType type)
{
  return itemColors[type];
}

static SDL_Texture *makeItemCapsule(ItemType type, TTF_Font *capsuleFont)
{
  const int W = GFX_ITEM_W * GFX_SS;
  const int H = GFX_ITEM_H * GFX_SS;
  SDL_Surface *s = createSurface(W, H);
  SDL_Color col = itemColors[type];

  for (int y = 0; y < H; y++)
  {
    for (int x = 0; x < W; x++)
    {
      float fx = (x + 0.5f) / GFX_SS;
      float fy = (y + 0.5f) / GFX_SS;
      float px = fx - GFX_ITEM_W / 2.0f;
      float py = fy - GFX_ITEM_H / 2.0f;
      float d = sdRoundBox(px, py, GFX_ITEM_W / 2.0f - 1.0f, GFX_ITEM_H / 2.0f - 1.0f, 7.0f);
      float a = covS(d);

      if (a <= 0)
      {
        put(s, x, y, 0, 0, 0, 0);
        continue;
      }

      float lum = 1.0f - 0.4f * (fy / GFX_ITEM_H);
      // Glass shine across the top
      float gd = fy - 3.4f;
      lum += 0.45f * expf(-gd * gd / 3.0f);

      float rim = clamp((d + 1.9f) / 1.9f, 0.0f, 1.0f);
      lum *= 1.0f - 0.42f * rim;

      put(s, x, y,
          (int)clamp(col.r * lum, 0, 255),
          (int)clamp(col.g * lum, 0, 255),
          (int)clamp(col.b * lum, 0, 255),
          (int)(a * 255));
    }
  }

  // Letter with a soft shadow
  SDL_Surface *shadow = TTF_RenderText_Blended(capsuleFont, itemLetters[type], 0,
                                               (SDL_Color){0, 0, 0, 200});
  SDL_Surface *text = TTF_RenderText_Blended(capsuleFont, itemLetters[type], 0,
                                             (SDL_Color){255, 255, 255, 255});

  if (shadow != NULL && text != NULL)
  {
    SDL_Rect dst = {
        W / 2 - text->w / 2,
        H / 2 - text->h / 2,
        text->w,
        text->h};

    SDL_Rect shadowDst = {dst.x + GFX_SS, dst.y + GFX_SS, dst.w, dst.h};
    SDL_BlitSurface(shadow, NULL, s, &shadowDst);
    SDL_BlitSurface(text, NULL, s, &dst);
  }

  if (shadow != NULL) SDL_DestroySurface(shadow);
  if (text != NULL) SDL_DestroySurface(text);

  return textureFrom(s, false);
}

// ---------------------------------------------------------------------------
// Nebula backgrounds
// ---------------------------------------------------------------------------

static SDL_Texture *makeNebula(const WorldTheme *theme, unsigned int seed)
{
  const int W = SCREEN_WIDTH;
  const int H = SCREEN_HEIGHT;
  SDL_Surface *s = createSurface(W, H);

  for (int y = 0; y < H; y++)
  {
    for (int x = 0; x < W; x++)
    {
      float n = 0.42f * noiseOctave(seed, (float)x, (float)y, 100, W, H) +
                0.27f * noiseOctave(seed + 7, (float)x, (float)y, 50, W, H) +
                0.19f * noiseOctave(seed + 13, (float)x, (float)y, 25, W, H) +
                0.12f * noiseOctave(seed + 21, (float)x, (float)y, 10, W, H);

      float shape = powf(n, 1.65f);

      // Slight dithering breaks up gradient banding
      shape += (hash2(seed + 99, x, y) - 0.5f) * 0.012f;
      shape = clamp(shape, 0.0f, 1.0f);

      float rr = lerp(theme->deep.r * 0.35f, theme->accent.r, shape);
      float gg = lerp(theme->deep.g * 0.35f, theme->accent.g, shape);
      float bb = lerp(theme->deep.b * 0.35f, theme->accent.b, shape);

      // The brightest wisps drift toward the glow color for richness
      float hot = clamp((shape - 0.55f) / 0.45f, 0.0f, 1.0f) * 0.35f;
      rr = lerp(rr, theme->glow.r, hot);
      gg = lerp(gg, theme->glow.g, hot);
      bb = lerp(bb, theme->glow.b, hot);

      // Horizontal vignette (vertical must stay uniform so the texture tiles)
      float vx = (x - W / 2.0f) / (W / 2.0f);
      float vig = 1.0f - 0.30f * vx * vx;
      rr *= vig;
      gg *= vig;
      bb *= vig;

      put(s, x, y, (int)rr, (int)gg, (int)bb, 255);
    }
  }

  // Baked far stars with a soft falloff
  seedRng(seed * 31 + 7);
  for (int i = 0; i < 170; i++)
  {
    int sx = (int)(rngFloat() * W);
    int sy = (int)(rngFloat() * H);
    int v = 60 + (int)(rngFloat() * 140);

    blend(s, sx, sy, v, v, (int)(v * 1.05f), 255);
    blend(s, sx + 1, sy, v, v, (int)(v * 1.05f), 90);
    blend(s, sx, sy + 1, v, v, (int)(v * 1.05f), 90);

    if (rngFloat() < 0.18f)
    {
      // A brighter star with a small cross flare
      blend(s, sx + 1, sy, v / 2, v / 2, v / 2, 200);
      blend(s, sx - 1, sy, v / 2, v / 2, v / 2, 200);
      blend(s, sx, sy + 1, v / 2, v / 2, v / 2, 200);
      blend(s, sx, sy - 1, v / 2, v / 2, v / 2, 200);
    }
  }

  return textureFrom(s, false);
}

// ---------------------------------------------------------------------------

void initializeGfx(void)
{
  texBrick = makeBrick();
  texBrickSolid = makeBrickSolid();
  texBrickGold = makeBrickGold();
  texCrystal = makeCrystal();
  texBombEmblem = makeBombEmblem();
  texCrackLight = makeCracks(2, 0xBADA55);
  texCrackHeavy = makeCracks(5, 0xDEADBEEF);

  texBall = makeBall();
  texGlow = makeGlow();
  texSquare = makeSquare();

  texStar = makeStar();
  texSparkle = makeSparkle();
  texFlow = makeFlow();
  texVignette = makeVignette();

  texPaddle[0] = makePaddle(100);
  texPaddle[1] = makePaddle(150);
  texPaddle[2] = makePaddle(60);
  texTurret = makeTurret();

  texEnemy[ENEMY_DRIFTER] = makeSpikyOrb(GFX_ENEMY_SIZE, 8.0f, 5.0f, 4,
                                         (SDL_Color){255, 80, 200, 255});
  texEnemy[ENEMY_DIVER] = makeDiver();
  texEnemy[ENEMY_SPLITTER] = makeSplitter();
  texEnemy[ENEMY_SHOOTER] = makeShooter();
  texEnemy[ENEMY_MINI] = makeSpikyOrb(GFX_ENEMY_MINI_SIZE, 4.5f, 3.0f, 3,
                                      (SDL_Color){255, 120, 220, 255});

  // Capsule letters are baked at the sprite supersample resolution
  // (loadFont multiplies by TEXT_SCALE internally, so divide it back out)
  TTF_Font *capsuleFont =
      loadFont("assets/font.ttf", 16.0f * GFX_SS / TEXT_SCALE);

  for (int i = 0; i < ITEM_COUNT; i++)
  {
    texItem[i] = makeItemCapsule((ItemType)i, capsuleFont);
  }

  TTF_CloseFont(capsuleFont);

  for (int i = 0; i < WORLD_COUNT + 1; i++)
  {
    texNebula[i] = makeNebula(&worldThemes[i], 1234u + i * 777u);
  }
}

void destroyGfx(void)
{
  SDL_Texture **all[] = {
      &texBrick, &texBrickSolid, &texBrickGold, &texCrystal, &texBombEmblem,
      &texCrackLight, &texCrackHeavy, &texBall, &texGlow, &texSquare,
      &texStar, &texSparkle, &texFlow, &texVignette,
      &texPaddle[0], &texPaddle[1], &texPaddle[2], &texTurret};

  for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++)
  {
    SDL_DestroyTexture(*all[i]);
    *all[i] = NULL;
  }

  for (int i = 0; i < ENEMY_TYPE_COUNT; i++)
  {
    SDL_DestroyTexture(texEnemy[i]);
    texEnemy[i] = NULL;
  }

  for (int i = 0; i < ITEM_COUNT; i++)
  {
    SDL_DestroyTexture(texItem[i]);
    texItem[i] = NULL;
  }

  for (int i = 0; i < WORLD_COUNT + 1; i++)
  {
    SDL_DestroyTexture(texNebula[i]);
    texNebula[i] = NULL;
  }
}
