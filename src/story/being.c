#include "being.h"
#include "../lib/gfx.h"
#include "../lib/vector.h"

// Pixels to a unit of face at the face's own depth, and how far the eye is from
// it. The head is about two and a half units from crown to chin, so at SCALE
// this is a face a little under two hundred pixels tall, and a distance of
// five puts enough perspective on it that turning the head reads as turning it.
#define SCALE 78.0f // the default, as a scene draws it
#define DISTANCE 5.0f

#define MAX_SEGS BEING_MAX_SEGS

// The mask, crown to chin: at each height, how far it reaches to either side
// and how far forward. Everything else on the face sits on this surface.
typedef struct Row
{
  float y, rx, rz;
} Row;

static const Row rows[] = {
    {1.10f, 0.30f, 0.25f},
    {0.95f, 0.62f, 0.50f},
    {0.70f, 0.84f, 0.68f},
    {0.40f, 0.92f, 0.76f},
    {0.10f, 0.90f, 0.80f},
    {-0.20f, 0.84f, 0.80f},
    {-0.50f, 0.72f, 0.74f},
    {-0.78f, 0.52f, 0.62f},
    {-1.00f, 0.30f, 0.50f},
    {-1.15f, 0.10f, 0.40f},
};

#define ROW_COUNT ((int)SDL_arraysize(rows))

// The grid's lines of longitude, and how far round the head they go: a little
// past the side, so that turning it shows that it is a mask and not a head.
#define MERIDIANS 13
#define U_MAX 1.9f

typedef enum SegKind
{
  SEG_GRID,     // the mask itself, faint
  SEG_FEATURE,  // brows, lids, nose, mouth
  SEG_EYE,      // irises and pupils
  SEG_ORNAMENT, // the circuitry on the forehead and the halo
  SEG_THIRD,    // the eye in the forehead
} SegKind;

typedef struct Seg
{
  Vec3 a, b;
  SegKind kind;
} Seg;

// What each mood does to the face; see BeingFace in being.h.
static const BeingFace expressions[STORY_MOOD_COUNT] = {
    [STORY_MOOD_CALM] = {0.0f, 0.0f, 0.0f, 0.0f, 0.9f, 0.0f, 0.0f, 0.0f},
    [STORY_MOOD_AMUSED] = {0.02f, 0.04f, 0.07f, 0.65f, 0.72f, 0.0f, 0.0f, 0.0f},
    [STORY_MOOD_STERN] = {-0.09f, 0.04f, 0.0f, -0.35f, 0.66f, 0.0f, 0.0f, 0.35f},
    [STORY_MOOD_PAINED] = {0.10f, -0.05f, 0.0f, -0.55f, 1.15f, 0.22f, 1.0f, 0.2f},
    [STORY_MOOD_SOFT] = {0.04f, 0.0f, 0.0f, 0.25f, 0.5f, 0.0f, 0.0f, 0.0f},
};

static Seg segs[MAX_SEGS];
static int segCount;

// xorshift32: nothing here needs better, and a face drawn in the back of a
// level must not take numbers out of the rand() its drops come from.
static float rngUnit(Being *b)
{
  b->rng ^= b->rng << 13;
  b->rng ^= b->rng >> 17;
  b->rng ^= b->rng << 5;

  return (b->rng & 0xffffff) / 16777216.0f;
}

static float rngRange(Being *b, float lo, float hi)
{
  return lo + (hi - lo) * rngUnit(b);
}

// ---------------------------------------------------------------------------
// The face, in its own space
// ---------------------------------------------------------------------------

static void addSeg(Vec3 a, Vec3 b, SegKind kind)
{
  if (segCount < MAX_SEGS)
  {
    segs[segCount++] = (Seg){a, b, kind};
  }
}

// The mask's reach at height `y`, between the two rows either side of it.
static void rowAt(float y, float *rx, float *rz)
{
  if (y >= rows[0].y)
  {
    *rx = rows[0].rx;
    *rz = rows[0].rz;
    return;
  }

  for (int i = 0; i < ROW_COUNT - 1; i++)
  {
    if (y >= rows[i + 1].y)
    {
      float t = (y - rows[i + 1].y) / (rows[i].y - rows[i + 1].y);

      *rx = lerp(rows[i + 1].rx, rows[i].rx, t);
      *rz = lerp(rows[i + 1].rz, rows[i].rz, t);
      return;
    }
  }

  *rx = rows[ROW_COUNT - 1].rx;
  *rz = rows[ROW_COUNT - 1].rz;
}

// A point on the face at (x, y), lifted `lift` off it.
static Vec3 onFace(float x, float y, float lift)
{
  float rx, rz;

  rowAt(y, &rx, &rz);

  float s = clamp(x / rx, -0.98f, 0.98f);

  return vec3(x, y, rz * sqrtf(1.0f - s * s) + lift);
}

static Vec3 gridPoint(int row, float u)
{
  return vec3(rows[row].rx * sinf(u), rows[row].y, rows[row].rz * cosf(u));
}

// The grid stays out of the eyes and the mouth, which are drawn over it and
// read better without a lattice through them.
static bool inFeature(Vec3 a, Vec3 b)
{
  float x = (a.x + b.x) * 0.5f;
  float y = (a.y + b.y) * 0.5f;
  float ex = (fabsf(x) - 0.36f) / 0.22f;
  float ey = (y - 0.03f) / 0.11f;
  float mx = x / 0.3f;
  float my = (y + 0.62f) / 0.12f;

  return ex * ex + ey * ey < 1.0f || mx * mx + my * my < 1.0f;
}

static void buildGrid(void)
{
  float step = 2.0f * U_MAX / (MERIDIANS - 1);

  for (int r = 0; r < ROW_COUNT; r++)
  {
    for (int j = 0; j < MERIDIANS - 1; j++)
    {
      Vec3 a = gridPoint(r, -U_MAX + step * j);
      Vec3 b = gridPoint(r, -U_MAX + step * (j + 1));

      if (!inFeature(a, b))
      {
        addSeg(a, b, SEG_GRID);
      }
    }
  }

  for (int j = 0; j < MERIDIANS; j++)
  {
    for (int r = 0; r < ROW_COUNT - 1; r++)
    {
      Vec3 a = gridPoint(r, -U_MAX + step * j);
      Vec3 b = gridPoint(r + 1, -U_MAX + step * j);

      if (!inFeature(a, b))
      {
        addSeg(a, b, SEG_GRID);
      }
    }
  }
}

static void buildBrows(Being *b)
{
  const BeingFace *f = &b->face;

  for (int side = -1; side <= 1; side += 2)
  {
    float lift = side < 0 ? f->browLift : 0.0f;
    Vec3 inner = onFace(side * 0.13f, 0.25f + f->browInner + lift, 0.03f);
    Vec3 mid = onFace(side * 0.34f, 0.31f + (f->browInner + f->browOuter) * 0.5f + lift, 0.03f);
    Vec3 outer = onFace(side * 0.56f, 0.25f + f->browOuter + lift, 0.02f);

    addSeg(inner, mid, SEG_FEATURE);
    addSeg(mid, outer, SEG_FEATURE);
  }
}

static void buildEyes(Being *b)
{
  const BeingFace *f = &b->face;
  float blink = b->blinkPhase < 1.0f ? 1.0f - sinf(b->blinkPhase * SDL_PI_F) : 1.0f;
  float open = f->eyeOpen * blink;

  // Where the eyes are pointed: at the look target, as far as the lids allow.
  float gazeX = clamp((b->lookX - b->x) / 300.0f, -1.0f, 1.0f) * 0.06f;
  float gazeY = -clamp((b->lookY - b->y) / 300.0f, -1.0f, 1.0f) * 0.035f;

  b->pupilOpen = open;

  for (int side = -1; side <= 1; side += 2)
  {
    const int K = 9;
    float cx = side * 0.36f;
    float cy = 0.03f;

    // A stern look drops the inner corners, which is most of what reads as a
    // glare on a face this simple.
    float tilt = -f->browInner * 0.6f;

    Vec3 upper[9], lower[9];

    for (int i = 0; i < K; i++)
    {
      float t = i / (float)(K - 1);
      float x = cx + (t - 0.5f) * 0.34f;
      float bump = sinf(t * SDL_PI_F);
      float inward = side > 0 ? 1.0f - t : t; // 1 at the corner nearest the nose
      float y = cy - tilt * inward * 0.5f;

      upper[i] = onFace(x, y + 0.11f * open * bump, 0.02f);
      lower[i] = onFace(x, y - 0.08f * open * bump, 0.02f);
    }

    for (int i = 0; i < K - 1; i++)
    {
      addSeg(upper[i], upper[i + 1], SEG_FEATURE);
      addSeg(lower[i], lower[i + 1], SEG_FEATURE);
    }

    Vec3 pupil = onFace(cx + gazeX, cy + gazeY * open, 0.035f);
    b->pupilAt[side < 0 ? 0 : 1] = pupil;

    if (open > 0.25f)
    {
      float r = 0.055f * clamp(open * 1.2f, 0.0f, 1.0f);
      const int ring = 10;

      for (int i = 0; i < ring; i++)
      {
        float a0 = i * 2.0f * SDL_PI_F / ring;
        float a1 = (i + 1) * 2.0f * SDL_PI_F / ring;

        addSeg(vec3Add(pupil, vec3(cosf(a0) * r, sinf(a0) * r * 0.85f, 0)),
               vec3Add(pupil, vec3(cosf(a1) * r, sinf(a1) * r * 0.85f, 0)), SEG_EYE);
      }

      float d = r * 0.4f;

      addSeg(vec3Add(pupil, vec3(-d, 0, 0)), vec3Add(pupil, vec3(0, d, 0)), SEG_EYE);
      addSeg(vec3Add(pupil, vec3(0, d, 0)), vec3Add(pupil, vec3(d, 0, 0)), SEG_EYE);
      addSeg(vec3Add(pupil, vec3(d, 0, 0)), vec3Add(pupil, vec3(0, -d, 0)), SEG_EYE);
      addSeg(vec3Add(pupil, vec3(0, -d, 0)), vec3Add(pupil, vec3(-d, 0, 0)), SEG_EYE);
    }
  }
}

static void buildNose(void)
{
  Vec3 bridge = onFace(0, 0.14f, 0.02f);
  Vec3 tip = onFace(0, -0.30f, 0.2f);
  Vec3 base = onFace(0, -0.38f, 0.06f);

  addSeg(bridge, tip, SEG_FEATURE);
  addSeg(tip, base, SEG_FEATURE);

  for (int side = -1; side <= 1; side += 2)
  {
    Vec3 wing = onFace(side * 0.12f, -0.36f, 0.03f);

    addSeg(tip, wing, SEG_FEATURE);
    addSeg(wing, base, SEG_FEATURE);
  }
}

static void buildMouth(Being *b)
{
  const BeingFace *f = &b->face;
  const int K = 11;
  float open = clamp(b->mouth + f->mouthRest, 0.0f, 1.0f);
  Vec3 upper[11], lower[11];

  for (int i = 0; i < K; i++)
  {
    float t = i / (float)(K - 1);
    float x = (t - 0.5f) * 0.46f;
    float bump = sinf(t * SDL_PI_F);
    float corner = (2.0f * t - 1.0f) * (2.0f * t - 1.0f);
    float y = -0.62f + f->smile * 0.07f * (corner - 0.4f);

    upper[i] = onFace(x, y + open * 0.035f * bump, 0.02f);
    lower[i] = onFace(x, y - open * 0.17f * bump, 0.02f);
  }

  // Shut, the two lips are one line, and drawing it twice would only double
  // it up.
  for (int i = 0; i < K - 1; i++)
  {
    addSeg(upper[i], upper[i + 1], SEG_FEATURE);

    if (open > 0.04f)
    {
      addSeg(lower[i], lower[i + 1], SEG_FEATURE);
    }
  }

  b->mouthAt = onFace(0, -0.62f - open * 0.07f, 0.0f);
}

// The eye in the forehead, and the circuitry running out of it.
static void buildCrown(Being *b)
{
  Vec3 centre = onFace(0, 0.6f, 0.04f);
  float spin = b->time * 0.8f;

  b->thirdEyeAt = centre;

  for (int ring = 0; ring < 2; ring++)
  {
    float w = ring == 0 ? 0.075f : 0.035f;
    float h = ring == 0 ? 0.11f : 0.05f;
    float a = ring == 0 ? spin : -spin * 1.6f;

    for (int i = 0; i < 4; i++)
    {
      float a0 = a + i * SDL_PI_F / 2;
      float a1 = a + (i + 1) * SDL_PI_F / 2;

      addSeg(vec3Add(centre, vec3(cosf(a0) * w, sinf(a0) * h, 0)),
             vec3Add(centre, vec3(cosf(a1) * w, sinf(a1) * h, 0)), SEG_THIRD);
    }
  }

  for (int side = -1; side <= 1; side += 2)
  {
    Vec3 a = onFace(side * 0.12f, 0.6f, 0.02f);
    Vec3 b = onFace(side * 0.30f, 0.6f, 0.02f);
    Vec3 c = onFace(side * 0.42f, 0.74f, 0.02f);
    Vec3 d = onFace(side * 0.24f, 0.47f, 0.02f);
    Vec3 e = onFace(side * 0.52f, 0.47f, 0.02f);

    addSeg(a, b, SEG_ORNAMENT);
    addSeg(b, c, SEG_ORNAMENT);
    addSeg(d, e, SEG_ORNAMENT);
  }
}

// Two rings behind the head, turning against each other, and three shards
// going round it.
static void buildHalo(Being *b)
{
  Vec3 centre = vec3(0, 0.15f, -0.55f);
  float t = b->time;

  const int A = 48;

  for (int i = 0; i < A; i++)
  {
    // A dash and a gap, going round
    if (i % 4 == 3)
    {
      continue;
    }

    float a0 = i * 2.0f * SDL_PI_F / A + t * 0.25f;
    float a1 = (i + 1) * 2.0f * SDL_PI_F / A + t * 0.25f;
    Vec3 p0 = vec3Rotate(vec3(cosf(a0) * 1.5f, sinf(a0) * 1.5f, 0), 0, 0.25f, 0);
    Vec3 p1 = vec3Rotate(vec3(cosf(a1) * 1.5f, sinf(a1) * 1.5f, 0), 0, 0.25f, 0);

    addSeg(vec3Add(centre, p0), vec3Add(centre, p1), SEG_ORNAMENT);

    if (i % 6 == 0)
    {
      Vec3 tick = vec3Rotate(vec3(cosf(a0) * 1.62f, sinf(a0) * 1.62f, 0), 0, 0.25f, 0);
      addSeg(vec3Add(centre, p0), vec3Add(centre, tick), SEG_ORNAMENT);
    }
  }

  const int B = 36;

  for (int i = 0; i < B; i++)
  {
    if (i % 3 == 2)
    {
      continue;
    }

    float a0 = i * 2.0f * SDL_PI_F / B - t * 0.4f;
    float a1 = (i + 1) * 2.0f * SDL_PI_F / B - t * 0.4f;
    Vec3 p0 = vec3Rotate(vec3(cosf(a0) * 1.32f, sinf(a0) * 1.32f, 0), 0.9f, 0.2f, 0);
    Vec3 p1 = vec3Rotate(vec3(cosf(a1) * 1.32f, sinf(a1) * 1.32f, 0), 0.9f, 0.2f, 0);

    addSeg(vec3Add(centre, p0), vec3Add(centre, p1), SEG_ORNAMENT);
  }

  for (int i = 0; i < 3; i++)
  {
    float a = t * 0.6f + i * 2.0f * SDL_PI_F / 3;
    Vec3 at = vec3(cosf(a) * 1.85f, 0.2f + sinf(t * 1.3f + i * 2.1f) * 0.25f, sinf(a) * 1.85f - 0.2f);
    float s = 0.09f;
    float spin = t * 2.0f + i;
    Vec3 v[3];

    for (int k = 0; k < 3; k++)
    {
      float ka = spin + k * 2.0f * SDL_PI_F / 3;
      v[k] = vec3Add(at, vec3Rotate(vec3(cosf(ka) * s, sinf(ka) * s, 0), a, 0.4f, 0));
    }

    addSeg(v[0], v[1], SEG_ORNAMENT);
    addSeg(v[1], v[2], SEG_ORNAMENT);
    addSeg(v[2], v[0], SEG_ORNAMENT);
  }
}

static void build(Being *b)
{
  segCount = 0;

  if (b->halo)
  {
    buildHalo(b);
  }

  buildGrid();
  buildCrown(b);
  buildBrows(b);
  buildEyes(b);
  buildNose();
  buildMouth(b);
}

// ---------------------------------------------------------------------------
// In the world
// ---------------------------------------------------------------------------

static VecCamera camera(const Being *b)
{
  return (VecCamera){b->x, b->y, b->scale, DISTANCE, 2.2f};
}

static Vec3 place(const Being *b, Vec3 p)
{
  // The slipped band moves with the face's own height, before it is turned,
  // so that it is a slice of the face and not a stripe across the screen.
  if (b->bandHold > 0 && p.y > b->bandY && p.y < b->bandY + b->bandH)
  {
    p.x += b->bandShift;
  }

  return vec3Rotate(p, b->yaw, b->pitch, b->roll);
}

static SDL_Color kindColor(const Being *b, SegKind kind, SDL_Color tint)
{
  SDL_Color warm = {255, 90, 80, 255};
  SDL_Color hot = mixColor(tint, warm, b->face.warmth);

  switch (kind)
  {
  case SEG_GRID:
    return tint;
  case SEG_FEATURE:
    return mixColor(hot, (SDL_Color){255, 255, 255, 255}, 0.45f);
  case SEG_EYE:
    return mixColor(hot, (SDL_Color){255, 255, 255, 255}, 0.75f);
  case SEG_THIRD:
    return mixColor(tint, (SDL_Color){255, 255, 255, 255}, 0.3f);
  default:
    return tint;
  }
}

static float kindAlpha(SegKind kind)
{
  switch (kind)
  {
  case SEG_GRID:
    return 0.26f;
  case SEG_ORNAMENT:
    return 0.5f;
  default:
    return 0.95f;
  }
}

static float kindWidth(SegKind kind)
{
  switch (kind)
  {
  case SEG_GRID:
    return 0.9f;
  case SEG_FEATURE:
  case SEG_EYE:
    return 1.4f;
  default:
    return 1.1f;
  }
}

// Where the scan line is that draws the face in and out, in the face's own
// height: below it is drawn, above it is not yet.
static float scanLine(const Being *b)
{
  return -1.7f + b->visible * 3.7f;
}

void beingReset(Being *b)
{
  SDL_memset(b, 0, sizeof(*b));

  b->x = SCREEN_WIDTH / 2.0f;
  b->y = -120.0f;
  b->targetX = b->x;
  b->targetY = 170.0f;
  b->lookX = b->x;
  b->lookY = 400.0f;
  b->scale = SCALE;
  b->eyeGlow = 1.0f;
  b->wander = 1.0f;
  b->turn = 1.0f;
  b->grid = 1.0f;
  b->halo = true;
  b->blinkPhase = 2.0f;
  b->blinkWait = 1.5f;
  b->mood = STORY_MOOD_CALM;
  b->face = expressions[STORY_MOOD_CALM];
  b->wantVisible = true;
  // A different stream for every face, and the same streams every time the
  // game is run, so that a capture of one comes out the same twice.
  static Uint32 streams = 0x9e3779b9u;
  streams += 0x6d2b79f5u;
  b->rng = streams | 1u;
}

void beingPlace(Being *b, float x, float y)
{
  b->x = x;
  b->y = y;
  b->vx = 0;
  b->vy = 0;
  b->targetX = x;
  b->targetY = y;
}

void beingSetMood(Being *b, StoryMood mood)
{
  b->mood = (unsigned)mood < STORY_MOOD_COUNT ? mood : STORY_MOOD_CALM;
}

void beingSetGlitch(Being *b, float glitch)
{
  b->glitch = clamp(glitch, 0.0f, 1.0f);
}

void beingSpeak(Being *b, float strength)
{
  b->mouth = SDL_max(b->mouth, clamp(strength, 0.0f, 1.0f));
}

void beingSetTarget(Being *b, float x, float y)
{
  b->targetX = x;
  b->targetY = y;
}

void beingLookAt(Being *b, float x, float y)
{
  b->lookX = x;
  b->lookY = y;
}

void beingSetVisible(Being *b, bool visible)
{
  b->wantVisible = visible;
}

void beingShatter(Being *b)
{
  if (b->shattered)
  {
    return;
  }

  build(b);

  VecCamera cam = camera(b);
  SDL_Color tint = {255, 240, 220, 255};

  b->shardCount = 0;

  for (int i = 0; i < segCount; i++)
  {
    SDL_FPoint p0, p1;

    if (!vecProject(&cam, place(b, segs[i].a), &p0, NULL) ||
        !vecProject(&cam, place(b, segs[i].b), &p1, NULL))
    {
      continue;
    }

    BeingShard *s = &b->shards[b->shardCount++];
    float mx = (p0.x + p1.x) * 0.5f;
    float my = (p0.y + p1.y) * 0.5f;
    float dx = mx - b->x;
    float dy = my - b->y;
    float dist = sqrtf(dx * dx + dy * dy) + 1.0f;
    float speed = rngRange(b, 60.0f, 360.0f) * (0.6f + dist / 160.0f);

    s->x = mx;
    s->y = my;
    s->dx0 = p0.x - mx;
    s->dy0 = p0.y - my;
    s->dx1 = p1.x - mx;
    s->dy1 = p1.y - my;
    s->vx = dx / dist * speed + rngRange(b, -40.0f, 40.0f);
    s->vy = dy / dist * speed + rngRange(b, -40.0f, 40.0f);
    s->angle = 0;
    s->spin = rngRange(b, -7.0f, 7.0f);
    s->width = kindWidth(segs[i].kind) * 1.2f;
    s->alpha = SDL_min(1.0f, kindAlpha(segs[i].kind) * 1.8f);
    s->color = kindColor(b, segs[i].kind, tint);
  }

  b->shattered = true;
  b->shatterT = 0;
}

void updateBeing(Being *b, float dt)
{
  b->time += dt;

  const BeingFace *want = &expressions[b->mood];
  BeingFace *face = &b->face;
  float ease = 1.0f - expf(-dt * 6.0f);

  face->browInner = lerp(face->browInner, want->browInner, ease);
  face->browOuter = lerp(face->browOuter, want->browOuter, ease);
  face->browLift = lerp(face->browLift, want->browLift, ease);
  face->smile = lerp(face->smile, want->smile, ease);
  face->eyeOpen = lerp(face->eyeOpen, want->eyeOpen, ease);
  face->mouthRest = lerp(face->mouthRest, want->mouthRest, ease);
  face->jitter = lerp(face->jitter, want->jitter, ease);
  face->warmth = lerp(face->warmth, want->warmth, ease);

  // Drift: a spring towards the target, and a slow wander round it so that it
  // never quite stands still.
  float t = b->time;
  float goalX = b->targetX + (sinf(t * 0.53f) * 22.0f + sinf(t * 1.31f) * 7.0f) * b->wander;
  float goalY = b->targetY + sinf(t * 0.87f) * 12.0f * b->wander;
  const float k = 9.0f;
  const float d = 5.5f;

  b->vx += ((goalX - b->x) * k - b->vx * d) * dt;
  b->vy += ((goalY - b->y) * k - b->vy * d) * dt;
  b->x += b->vx * dt;
  b->y += b->vy * dt;

  // It turns its head to what it is looking at, banks into the way it is
  // moving, and when it is in pain it cannot keep still.
  float jitter = b->face.jitter;
  float yaw = clamp((b->lookX - b->x) / 380.0f, -1.0f, 1.0f) * 0.55f * b->turn +
              sinf(t * 0.41f) * 0.12f + rngRange(b, -1.0f, 1.0f) * 0.03f * jitter;
  float pitch = clamp((b->lookY - b->y) / 420.0f, -0.6f, 1.0f) * 0.42f * b->turn +
                sinf(t * 0.67f) * 0.05f;
  float roll = clamp(-b->vx * 0.0012f, -0.3f, 0.3f) + sinf(t * 0.59f) * 0.05f +
               rngRange(b, -1.0f, 1.0f) * 0.03f * jitter;
  float turn = 1.0f - expf(-dt * 4.0f);

  b->yaw += (yaw - b->yaw) * turn;
  b->pitch += (pitch - b->pitch) * turn;
  b->roll += (roll - b->roll) * turn;

  b->mouth *= expf(-dt * 11.0f);

  if (b->blinkPhase < 1.0f)
  {
    b->blinkPhase += dt / 0.16f;
  }
  else
  {
    b->blinkWait -= dt;

    if (b->blinkWait <= 0)
    {
      b->blinkPhase = 0;
      b->blinkWait = rngRange(b, 2.2f, 5.5f);
    }
  }

  float target = b->wantVisible ? 1.0f : 0.0f;
  float step = dt / 1.1f;

  b->visible = fabsf(target - b->visible) <= step
                      ? target
                      : b->visible + (target > b->visible ? step : -step);

  // The slipped band: taken up now and then, held for a few frames, let go.
  if (b->bandHold > 0)
  {
    b->bandHold -= dt;
  }
  else if (rngUnit(b) < b->glitch * 0.06f)
  {
    b->bandY = rngRange(b, -1.2f, 1.0f);
    b->bandH = rngRange(b, 0.12f, 0.35f);
    b->bandShift = rngRange(b, -0.25f, 0.25f) * (0.4f + b->glitch);
    b->bandHold = rngRange(b, 0.05f, 0.18f);
  }

  if (b->shattered)
  {
    b->shatterT += dt;

    float drag = expf(-dt * 0.8f);

    for (int i = 0; i < b->shardCount; i++)
    {
      BeingShard *s = &b->shards[i];

      s->x += s->vx * dt;
      s->y += s->vy * dt;
      s->vx *= drag;
      s->vy *= drag;
      s->angle += s->spin * dt;
    }
  }
}

static void drawShards(Being *b, float alpha)
{
  float t = b->shatterT;
  float fade = clamp(1.0f - t / 2.6f, 0.0f, 1.0f);

  for (int i = 0; i < b->shardCount && fade > 0; i++)
  {
    const BeingShard *s = &b->shards[i];
    float c = cosf(s->angle);
    float sn = sinf(s->angle);

    vecLine(s->x + s->dx0 * c - s->dy0 * sn, s->y + s->dx0 * sn + s->dy0 * c,
            s->x + s->dx1 * c - s->dy1 * sn, s->y + s->dx1 * sn + s->dy1 * c,
            s->width, s->color, s->alpha * fade * alpha);
  }

  // The shock of it, going out.
  if (t < 1.4f)
  {
    float grow = 1.0f - powf(1.0f - t / 1.4f, 3.0f);
    float a = (1.0f - t / 1.4f) * alpha;

    vecPolygon(b->x, b->y, 30.0f + grow * 560.0f, 64, t, 2.4f,
               (SDL_Color){255, 240, 220, 255}, a);
    vecPolygon(b->x, b->y, 20.0f + grow * 380.0f, 6, -t * 0.7f, 1.4f,
               (SDL_Color){255, 200, 150, 255}, a * 0.7f);
  }

  vecFlush();
}

static void drawGlow(const Being *b, Vec3 at, const VecCamera *cam, float size,
                     SDL_Color color, float alpha)
{
  SDL_FPoint p;
  float depth;

  alpha *= b->eyeGlow;

  if (alpha <= 0.01f || !vecProject(cam, place(b, at), &p, &depth))
  {
    return;
  }

  float s = size * depth * b->scale / SCALE;

  SDL_SetTextureColorMod(texGlow, color.r, color.g, color.b);
  SDL_SetTextureAlphaMod(texGlow, (Uint8)(255 * clamp(alpha, 0.0f, 1.0f)));
  SDL_FRect dst = {p.x - s / 2, p.y - s / 2, s, s};
  SDL_RenderTexture(renderer, texGlow, NULL, &dst);
}

void drawBeing(Being *b, SDL_Color tint, float alpha)
{
  if (b->shattered)
  {
    b->penOrigin = (SDL_FPoint){b->x, b->y};
    drawShards(b, alpha);
    return;
  }

  if (b->visible <= 0.001f || alpha <= 0.001f)
  {
    return;
  }

  build(b);

  VecCamera cam = camera(b);
  float scan = scanLine(b);
  bool scanning = b->visible < 1.0f;
  SDL_Color white = {255, 255, 255, 255};

  // A ghost of the lit parts, knocked off to one side and in the wrong colour,
  // for a face that is coming apart. Drawn first, so the face is over it.
  if (b->glitch > 0.1f)
  {
    VecCamera ghost = cam;
    ghost.cx += sinf(b->time * 13.0f) * b->glitch * 9.0f;
    ghost.cy += cosf(b->time * 7.0f) * b->glitch * 3.0f;

    for (int i = 0; i < segCount; i++)
    {
      if (segs[i].kind == SEG_GRID || fmaxf(segs[i].a.y, segs[i].b.y) > scan)
      {
        continue;
      }

      vecLine3(&ghost, place(b, segs[i].a), place(b, segs[i].b), 1.0f,
               (SDL_Color){255, 60, 200, 255}, b->glitch * 0.35f * alpha);
    }
  }

  for (int i = 0; i < segCount; i++)
  {
    const Seg *s = &segs[i];
    float top = fmaxf(s->a.y, s->b.y);

    if (top > scan)
    {
      continue;
    }

    // Flickering out, now and then, the more the worse it is.
    if (b->glitch > 0 && rngUnit(b) < b->glitch * 0.16f)
    {
      continue;
    }

    SDL_Color color = kindColor(b, s->kind, tint);
    float a = kindAlpha(s->kind) * alpha * (s->kind == SEG_GRID ? b->grid : 1.0f);

    // What the scan line has only just reached is lit white-hot.
    if (scanning && scan - top < 0.25f)
    {
      color = mixColor(color, white, 0.8f);
      a = fmaxf(a, 0.9f * alpha);
    }

    vecLine3(&cam, place(b, s->a), place(b, s->b), kindWidth(s->kind), color, a);
  }

  // The scan line itself, round the face at the height it has reached.
  if (scanning && scan > rows[ROW_COUNT - 1].y && scan < rows[0].y)
  {
    float rx, rz;
    const int K = 16;

    rowAt(scan, &rx, &rz);

    for (int i = 0; i < K; i++)
    {
      float u0 = -U_MAX + 2.0f * U_MAX * i / K;
      float u1 = -U_MAX + 2.0f * U_MAX * (i + 1) / K;

      vecLine3(&cam, place(b, vec3(rx * 1.05f * sinf(u0), scan, rz * 1.05f * cosf(u0))),
               place(b, vec3(rx * 1.05f * sinf(u1), scan, rz * 1.05f * cosf(u1))), 1.6f,
               white, 0.9f * alpha);
    }
  }

  vecFlush();

  // The glows: the pupils, the eye in the forehead, and the inside of the
  // mouth while it is open. All of them wait for the scan line.
  SDL_Color eyeColor = kindColor(b, SEG_EYE, tint);
  float lit = alpha * clamp((scan - 0.1f) / 0.4f, 0.0f, 1.0f);

  for (int i = 0; i < 2; i++)
  {
    drawGlow(b, b->pupilAt[i], &cam, 30.0f, eyeColor, 0.85f * b->pupilOpen * lit);
  }

  float pulse = 0.7f + 0.3f * sinf(b->time * 3.1f);

  drawGlow(b, b->thirdEyeAt, &cam, 40.0f, kindColor(b, SEG_THIRD, tint),
           0.8f * pulse * alpha * clamp((scan - 0.65f) / 0.3f, 0.0f, 1.0f));

  float mouthOpen = clamp(b->mouth + b->face.mouthRest, 0.0f, 1.0f);

  drawGlow(b, b->mouthAt, &cam, 56.0f, kindColor(b, SEG_FEATURE, tint),
           0.6f * mouthOpen * alpha * clamp((scan + 0.5f) / 0.3f, 0.0f, 1.0f));

  SDL_SetTextureColorMod(texGlow, 255, 255, 255);
  SDL_SetTextureAlphaMod(texGlow, 255);

  SDL_FPoint pen;

  if (vecProject(&cam, place(b, b->thirdEyeAt), &pen, NULL))
  {
    b->penOrigin = pen;
  }
}
