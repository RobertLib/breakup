#include "starfield.h"
#include "gfx.h"

#include <limits.h>

#define DUST_STARS 70
#define STARS_PER_LAYER 55
#define MAX_METEORS 3

// Geometry layer: a lattice, drifting wireframe polygons and the occasional
// pulse ring, sitting between the nebula and the stars so the field reads as
// structure behind the dust rather than as something the ball could hit.
//
// Its whole layout is rolled per level, from a seed that is the level number:
// two levels running never look alike, and one level always looks like itself.
#define MAX_GRID_NODES 34
#define MAX_WIRE_SHAPES 10
#define MAX_RINGS 3
#define GEO_FAMILIES 3

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

// Lattice layouts. Which one a level gets is decided by its number, so a world
// of five levels walks through most of them.
typedef enum GeoStyle
{
  GEO_ORTHO,      // upright lattice
  GEO_DIAGONAL,   // the same weave turned 45 degrees
  GEO_TRIANGULAR, // three line families, 60 degrees apart
  GEO_RADIAL,     // spokes and rings around a point off the top of the field
  GEO_DOTS,       // no lines at all: a matrix of dots with a wave crossing it
  GEO_STYLE_COUNT
} GeoStyle;

// A lit intersection, held as the place on the screen it wants to sit rather
// than as a position or as a pair of line indices. Each frame it is snapped to
// whichever intersection is nearest that spot, which keeps it exactly on the
// crossing whatever angles the style uses, keeps it on screen however far the
// level has scrolled, and hides the recycling: when the lattice slides a whole
// cell the node hops one crossing over, and one crossing looks like the next.
typedef struct GridNode
{
  float tx, ty; // the spot it sits near, in screen space (lattice styles)
  int spoke;    // GEO_RADIAL: which spoke it sits on
  int ring;     // GEO_RADIAL: which ring
  float phase;  // pulse phase offset
} GridNode;

// Slowly rotating outlined polygons, parallaxed against the lattice
typedef struct WireShape
{
  float x, y;
  float radius;
  int sides;
  float angle;
  float spin;     // radians per second
  float parallax; // share of the scroll it takes
  float bobPhase;
  float drift; // sideways drift, px/s
  bool ringed; // draws a second polygon inside at half size
} WireShape;

// Expanding outlines, fired every few seconds from a random point
typedef struct PulseRing
{
  float x, y;
  float life;
  float maxLife;
  float maxRadius;
  int sides;
  float angle;
  bool active;
} PulseRing;

// Everything a reseed decides. Rolled in reseedGeometry(), read by the draws.
static struct
{
  int variant; // the level this was rolled for; BACKGROUND_MENU for the menus
  GeoStyle style;

  int familyCount;
  float angle[GEO_FAMILIES];
  float spacing;
  float parallax;
  float scanSpeed; // px/s along the lattice normal, either sign
  float scanWidth;
  float lineAlpha;
  float scanAlpha;

  // GEO_RADIAL only
  float centerX, centerY;
  int spokes;
  float spokeSpin;

  int nodeCount;
  GridNode nodes[MAX_GRID_NODES];

  int shapeCount;
  WireShape shapes[MAX_WIRE_SHAPES];

  float ringMin, ringMax; // seconds between pulse rings
  int ringSides;
} geo = {.variant = INT_MIN};

static PulseRing rings[MAX_RINGS];
static float ringTimer;

// The layout RNG is its own, seeded off the level rather than off the clock:
// frand() is the shared one and the shared one is where the starfield, the
// power-up drops and the enemies come from, so drawing from it here would make
// the background depend on how much of the level had been played.
static unsigned int geoRng;

static float grand(void)
{
  geoRng = geoRng * 1664525u + 1013904223u;

  return (float)((geoRng >> 8) & 0xFFFFFFu) / (float)0x1000000;
}

static float grandRange(float a, float b)
{
  return a + grand() * (b - a);
}

static int grandInt(int a, int b) // inclusive
{
  return a + (int)(grand() * (float)(b - a + 1));
}

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

  // The geometry layer is rolled lazily, on the first frame that asks for a
  // variant, because only the caller knows which level is about to be drawn.
  geo.variant = INT_MIN;
}

// Rolls a fresh layout for `variant` - a level number, or BACKGROUND_MENU.
static void reseedGeometry(int variant)
{
  geo.variant = variant;

  bool menu = variant == BACKGROUND_MENU;
  int level = menu ? 0 : variant;

  // Knuth's multiplicative hash, so that adjacent levels - which is the only
  // pair anybody ever sees side by side - land far apart in the sequence.
  geoRng = (unsigned int)(level + 1) * 2654435761u ^ 0x9E3779B9u;

  // Stepping the style by one or two each level guarantees the change is
  // visible from one level to the next, rather than merely likely.
  geo.style = menu ? GEO_ORTHO
                   : (GeoStyle)((level + level / GEO_STYLE_COUNT) %
                                GEO_STYLE_COUNT);

  geo.spacing = grandRange(58.0f, 104.0f);
  geo.parallax = grandRange(0.3f, 0.62f);
  geo.scanSpeed = grandRange(55.0f, 135.0f) * (grand() < 0.5f ? -1.0f : 1.0f);
  geo.scanWidth = grandRange(90.0f, 190.0f);
  geo.lineAlpha = grandRange(12.0f, 20.0f);
  geo.scanAlpha = grandRange(45.0f, 75.0f);

  switch (geo.style)
  {
  case GEO_DIAGONAL:
    geo.familyCount = 2;
    geo.angle[0] = SDL_PI_F * 0.25f;
    geo.angle[1] = SDL_PI_F * 0.75f;
    break;

  case GEO_TRIANGULAR:
    geo.familyCount = 3;
    geo.angle[0] = 0;
    geo.angle[1] = SDL_PI_F / 3.0f;
    geo.angle[2] = 2.0f * SDL_PI_F / 3.0f;
    break;

  case GEO_RADIAL:
    geo.familyCount = 0;
    geo.centerX = SCREEN_WIDTH * grandRange(0.2f, 0.8f);
    geo.centerY = SCREEN_HEIGHT * grandRange(-0.25f, 0.3f);
    geo.spokes = grandInt(9, 18);
    geo.spokeSpin = grandRange(-0.05f, 0.05f);
    break;

  case GEO_DOTS:
    geo.familyCount = 0;
    geo.spacing = grandRange(40.0f, 62.0f);
    break;

  case GEO_ORTHO:
  case GEO_STYLE_COUNT:
  default:
    geo.familyCount = 2;
    geo.angle[0] = 0;
    geo.angle[1] = SDL_PI_F * 0.5f;
    break;
  }

  // Nodes need two families to sit between, and the styles without lines draw
  // their own points anyway
  geo.nodeCount = geo.familyCount >= 2 ? grandInt(16, MAX_GRID_NODES)
                                       : (geo.style == GEO_RADIAL ? 14 : 0);

  for (int i = 0; i < geo.nodeCount; i++)
  {
    geo.nodes[i] = (GridNode){
        grand() * SCREEN_WIDTH,
        grand() * SCREEN_HEIGHT,
        grandInt(0, 31),
        grandInt(1, 8),
        grand() * 6.28f};
  }

  geo.shapeCount = grandInt(4, MAX_WIRE_SHAPES);

  // Each level leans on one polygon, with the odd stranger among them, so the
  // shapes look like a set rather than like a bag of leftovers
  int favourite = grandInt(3, 7);

  for (int i = 0; i < geo.shapeCount; i++)
  {
    geo.shapes[i] = (WireShape){
        grand() * SCREEN_WIDTH,
        grand() * SCREEN_HEIGHT,
        grandRange(26.0f, 95.0f),
        grand() < 0.7f ? favourite : grandInt(3, 8),
        grand() * 6.28f,
        grandRange(-0.4f, 0.4f),
        grandRange(0.12f, 0.55f),
        grand() * 6.28f,
        grandRange(-11.0f, 11.0f),
        grand() < 0.4f};
  }

  geo.ringMin = grandRange(2.5f, 4.5f);
  geo.ringMax = geo.ringMin + grandRange(1.5f, 3.5f);
  geo.ringSides = geo.style == GEO_TRIANGULAR ? (grand() < 0.5f ? 3 : 6)
                                              : (grand() < 0.4f ? 4 : 24);

  for (int i = 0; i < MAX_RINGS; i++)
  {
    rings[i].active = false;
  }

  ringTimer = grandRange(1.0f, 3.0f);
}

// Outline of a regular polygon, drawn with the current draw color
static void strokePolygon(float cx, float cy, float radius, int sides,
                          float angle)
{
  float px = 0, py = 0;

  for (int i = 0; i <= sides; i++)
  {
    float a = angle + i * (2.0f * SDL_PI_F / sides);
    float x = cx + cosf(a) * radius;
    float y = cy + sinf(a) * radius;

    if (i > 0)
    {
      SDL_RenderLine(renderer, px, py, x, y);
    }

    px = x;
    py = y;
  }
}

// How far family `f` has slid along its own normal. A scroll straight down the
// world moves a tilted family by the vertical part of its normal, which is why
// the diagonal lattices creep sideways as the level scrolls and the upright one
// does not.
static float familyShift(int f, float scrollY)
{
  return scrollY * geo.parallax * cosf(geo.angle[f]);
}

// Where the scan is, measured along the same normal
static float familySweep(int f)
{
  float span = SCREEN_WIDTH + SCREEN_HEIGHT;
  float t = gameTime * geo.scanSpeed + f * 40.0f;

  return fmodf(fmodf(t, span) + span, span) - span * 0.5f;
}

static void drawNode(float x, float y, SDL_Color tint, float pulse, float lit)
{
  float size = 10.0f + 7.0f * pulse + 14.0f * lit;
  Uint8 a = (Uint8)clamp(26.0f + 44.0f * pulse + 110.0f * lit, 0.0f, 255.0f);

  SDL_SetTextureColorMod(texGlow, tint.r, tint.g, tint.b);
  SDL_SetTextureAlphaMod(texGlow, a);
  SDL_FRect dst = {x - size / 2, y - size / 2, size, size};
  SDL_RenderTexture(renderer, texGlow, NULL, &dst);
}

// One family of parallel lines at geo.angle[f], spaced geo.spacing apart and
// clipped by the renderer rather than by us: each is drawn as a segment long
// enough to cross the window from any angle.
static void drawLineFamily(int f, SDL_Color tint, float scrollY)
{
  float ca = cosf(geo.angle[f]);
  float sa = sinf(geo.angle[f]);
  float nx = -sa, ny = ca;

  float cx = SCREEN_WIDTH / 2.0f;
  float cy = SCREEN_HEIGHT / 2.0f;
  float reach = SCREEN_WIDTH + SCREEN_HEIGHT;

  float shift = familyShift(f, scrollY);
  float frac = fmodf(shift, geo.spacing);
  float sweep = familySweep(f);

  int count = (int)(reach / (2.0f * geo.spacing)) + 2;

  for (int k = -count; k <= count; k++)
  {
    float d = k * geo.spacing + frac;
    float s = (d - sweep) / geo.scanWidth;
    float lit = expf(-s * s);
    float shimmer = 0.6f + 0.4f * sinf(gameTime * 0.9f + k * 0.7f + f);
    Uint8 a = (Uint8)clamp(geo.lineAlpha * shimmer + geo.scanAlpha * lit,
                           0.0f, 255.0f);

    float bx = cx + nx * d;
    float by = cy + ny * d;

    SDL_SetRenderDrawColor(renderer, tint.r, tint.g, tint.b, a);
    SDL_RenderLine(renderer, bx - ca * reach, by - sa * reach,
                   bx + ca * reach, by + sa * reach);
  }
}

// The crossing of families 0 and 1 nearest to the node's spot on the screen.
// `outD` comes back as its distance along family 0's normal, which is what the
// scan is measured in.
static void latticeNode(const GridNode *n, float scrollY,
                        float *outX, float *outY, float *outD)
{
  float a0 = geo.angle[0], a1 = geo.angle[1];
  float f0 = fmodf(familyShift(0, scrollY), geo.spacing);
  float f1 = fmodf(familyShift(1, scrollY), geo.spacing);

  // The spot, in distances along the two normals
  float px = n->tx - SCREEN_WIDTH / 2.0f;
  float py = n->ty - SCREEN_HEIGHT / 2.0f;
  float t0 = -px * sinf(a0) + py * cosf(a0);
  float t1 = -px * sinf(a1) + py * cosf(a1);

  // Rounded to the nearest line of each family
  float d0 = roundf((t0 - f0) / geo.spacing) * geo.spacing + f0;
  float d1 = roundf((t1 - f1) / geo.spacing) * geo.spacing + f1;

  // Solving p . n0 = d0 and p . n1 = d1 for p, with n = (-sin, cos).
  // The determinant is sin(a1 - a0), which is never zero: no two families of
  // one style share an angle.
  float det = sinf(a1 - a0);

  *outX = SCREEN_WIDTH / 2.0f + (d0 * cosf(a1) - d1 * cosf(a0)) / det;
  *outY = SCREEN_HEIGHT / 2.0f + (d0 * sinf(a1) - d1 * sinf(a0)) / det;
  *outD = d0;
}

static void drawLattice(SDL_Color tint, float scrollY)
{
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);

  for (int f = 0; f < geo.familyCount; f++)
  {
    drawLineFamily(f, tint, scrollY);
  }

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

  float sweep = familySweep(0);

  for (int i = 0; i < geo.nodeCount; i++)
  {
    const GridNode *n = &geo.nodes[i];
    float x, y, d;

    latticeNode(n, scrollY, &x, &y, &d);

    float s = (d - sweep) / geo.scanWidth;

    drawNode(x, y, tint, 0.5f + 0.5f * sinf(gameTime * 1.6f + n->phase),
             expf(-s * s));
  }

  SDL_SetTextureColorMod(texGlow, 255, 255, 255);
  SDL_SetTextureAlphaMod(texGlow, 255);
}

// Spokes and rings around a point usually off the top of the field, which is
// the one layout here with a vanishing point rather than a horizon.
static void drawRadialLattice(SDL_Color tint, float scrollY)
{
  float cx = geo.centerX;
  float cy = geo.centerY - scrollY * geo.parallax * 0.25f;
  float reach = SCREEN_WIDTH + SCREEN_HEIGHT;
  float spin = gameTime * geo.spokeSpin;

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);

  for (int i = 0; i < geo.spokes; i++)
  {
    float a = spin + i * (2.0f * SDL_PI_F / geo.spokes);
    float shimmer = 0.5f + 0.5f * sinf(gameTime * 0.8f + i * 0.9f);

    SDL_SetRenderDrawColor(renderer, tint.r, tint.g, tint.b,
                           (Uint8)(geo.lineAlpha * shimmer));
    SDL_RenderLine(renderer, cx, cy, cx + cosf(a) * reach, cy + sinf(a) * reach);
  }

  // Rings march outward on their own rather than waiting for the scroll, so
  // the layout still breathes on a level that never scrolls
  float march = fmodf(gameTime * 14.0f + scrollY * geo.parallax, geo.spacing);
  int count = (int)(reach / geo.spacing) + 1;

  for (int k = 1; k <= count; k++)
  {
    float radius = k * geo.spacing + march;
    float s = (radius - fabsf(familySweep(0))) / geo.scanWidth;
    float lit = expf(-s * s);

    SDL_SetRenderDrawColor(renderer, tint.r, tint.g, tint.b,
                           (Uint8)clamp(geo.lineAlpha * 0.9f +
                                            geo.scanAlpha * lit,
                                        0.0f, 255.0f));
    strokePolygon(cx, cy, radius, 48, 0);
  }

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

  for (int i = 0; i < geo.nodeCount; i++)
  {
    const GridNode *n = &geo.nodes[i];
    float a = spin + (n->spoke % geo.spokes) * (2.0f * SDL_PI_F / geo.spokes);
    float radius = n->ring * geo.spacing + march;

    drawNode(cx + cosf(a) * radius, cy + sinf(a) * radius, tint,
             0.5f + 0.5f * sinf(gameTime * 1.5f + n->phase), 0.0f);
  }

  SDL_SetTextureColorMod(texGlow, 255, 255, 255);
  SDL_SetTextureAlphaMod(texGlow, 255);
}

// No lines: a matrix of dots with a diagonal wave running through it.
static void drawDotMatrix(SDL_Color tint, float scrollY)
{
  float offset = fmodf(scrollY * geo.parallax, geo.spacing);
  if (offset < 0)
  {
    offset += geo.spacing;
  }

  for (float y = offset - geo.spacing; y < SCREEN_HEIGHT; y += geo.spacing)
  {
    for (float x = 0; x < SCREEN_WIDTH + geo.spacing; x += geo.spacing)
    {
      float wave = sinf((x + y) * 0.012f - gameTime * 1.8f);
      float pulse = 0.5f + 0.5f * wave;
      float crest = powf(pulse, 6.0f);

      // The dots are the whole layout here rather than a highlight on top of
      // one, so they carry more weight than the nodes of a lattice do
      float size = 6.0f + 6.0f * pulse + 14.0f * crest;
      Uint8 a = (Uint8)clamp(30.0f + 60.0f * pulse + 150.0f * crest,
                             0.0f, 255.0f);

      SDL_SetTextureColorMod(texGlow, tint.r, tint.g, tint.b);
      SDL_SetTextureAlphaMod(texGlow, a);
      SDL_FRect dst = {x - size / 2, y - size / 2, size, size};
      SDL_RenderTexture(renderer, texGlow, NULL, &dst);
    }
  }

  SDL_SetTextureColorMod(texGlow, 255, 255, 255);
  SDL_SetTextureAlphaMod(texGlow, 255);
}

static void drawGeometry(SDL_Color tint, float scrollY)
{
  switch (geo.style)
  {
  case GEO_RADIAL:
    drawRadialLattice(tint, scrollY);
    break;

  case GEO_DOTS:
    drawDotMatrix(tint, scrollY);
    break;

  default:
    drawLattice(tint, scrollY);
    break;
  }
}

static void drawWireShapes(SDL_Color tint, float scrollY)
{
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);

  for (int i = 0; i < geo.shapeCount; i++)
  {
    WireShape *s = &geo.shapes[i];

    // Presentation, like the meteors: it keeps turning through a hit stop
    s->angle += s->spin * (float)realDt;

    float x = s->x + s->drift * gameTime;
    x = fmodf(x, SCREEN_WIDTH + 240.0f);
    if (x < 0)
    {
      x += SCREEN_WIDTH + 240.0f;
    }
    x -= 120.0f;

    float y = fmodf(s->y + scrollY * s->parallax, (float)SCREEN_HEIGHT + 240.0f);
    if (y < 0)
    {
      y += SCREEN_HEIGHT + 240.0f;
    }
    y -= 120.0f;

    float breathe = 1.0f + 0.06f * sinf(gameTime * 0.8f + s->bobPhase);
    float radius = s->radius * breathe;

    // Nearer shapes (more parallax) sit slightly brighter
    Uint8 a = (Uint8)(26.0f + 34.0f * s->parallax);

    SDL_SetRenderDrawColor(renderer, tint.r, tint.g, tint.b, a);
    strokePolygon(x, y, radius, s->sides, s->angle);

    if (s->ringed)
    {
      SDL_SetRenderDrawColor(renderer, tint.r, tint.g, tint.b, (Uint8)(a * 0.7f));
      strokePolygon(x, y, radius * 0.5f, s->sides, -s->angle * 1.6f);
    }

    // A vertex marker so the rotation is legible even at these alphas
    float vx = x + cosf(s->angle) * radius;
    float vy = y + sinf(s->angle) * radius;
    float vs = 12.0f;

    SDL_SetTextureColorMod(texGlow, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(texGlow, (Uint8)(a * 1.6f));
    SDL_FRect dst = {vx - vs / 2, vy - vs / 2, vs, vs};
    SDL_RenderTexture(renderer, texGlow, NULL, &dst);
  }

  SDL_SetTextureColorMod(texGlow, 255, 255, 255);
  SDL_SetTextureAlphaMod(texGlow, 255);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
}

static void updateAndDrawRings(SDL_Color tint)
{
  ringTimer -= (float)realDt;

  if (ringTimer <= 0)
  {
    ringTimer = frandRange(geo.ringMin, geo.ringMax);

    for (int i = 0; i < MAX_RINGS; i++)
    {
      if (!rings[i].active)
      {
        rings[i] = (PulseRing){
            frandRange(SCREEN_WIDTH * 0.1f, SCREEN_WIDTH * 0.9f),
            frandRange(SCREEN_HEIGHT * 0.1f, SCREEN_HEIGHT * 0.9f),
            0,
            frandRange(2.2f, 3.4f),
            frandRange(140.0f, 280.0f),
            geo.ringSides,
            frand() * 6.28f,
            true};
        break;
      }
    }
  }

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);

  for (int i = 0; i < MAX_RINGS; i++)
  {
    PulseRing *r = &rings[i];

    if (!r->active)
    {
      continue;
    }

    r->life += (float)realDt;

    if (r->life >= r->maxLife)
    {
      r->active = false;
      continue;
    }

    float t = r->life / r->maxLife;

    // Fast out of the gate, then easing to a stop as it fades
    float grow = 1.0f - powf(1.0f - t, 3.0f);
    float fade = (1.0f - t) * (t < 0.1f ? t / 0.1f : 1.0f);

    SDL_SetRenderDrawColor(renderer, tint.r, tint.g, tint.b,
                           (Uint8)(70.0f * fade));
    strokePolygon(r->x, r->y, r->maxRadius * grow, r->sides,
                  r->angle + t * 0.6f);

    SDL_SetRenderDrawColor(renderer, tint.r, tint.g, tint.b,
                           (Uint8)(30.0f * fade));
    strokePolygon(r->x, r->y, r->maxRadius * grow * 0.78f, r->sides,
                  r->angle - t * 0.4f);
  }

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
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

void drawBackground(int worldIndex, int variant, float scrollY)
{
  worldIndex = clamp(worldIndex, 0, WORLD_COUNT);
  const WorldTheme *theme = &worldThemes[worldIndex];
  SDL_Texture *nebula = texNebula[worldIndex];

  if (variant != geo.variant)
  {
    reseedGeometry(variant);
  }

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

  drawGeometry(theme->glow, scrollY);

  // Drifting cosmic wisps: two layers moving at different speeds keep
  // the background alive even when the camera is still
  drawFlowLayer(theme->accent, 34, 560.0f,
                -gameTime * 7.0f, scrollY * 0.22f + gameTime * 4.0f);
  drawFlowLayer(theme->glow, 22, 340.0f,
                gameTime * 11.0f, scrollY * 0.4f - gameTime * 6.0f);

  drawWireShapes(theme->glow, scrollY);
  updateAndDrawRings(theme->glow);

  drawStarLayer(dustStars, DUST_STARS, 0.2f, scrollY, 0.4f, false);
  drawStarLayer(farStars, STARS_PER_LAYER, 0.35f, scrollY, 0.6f, false);
  drawStarLayer(nearStars, STARS_PER_LAYER, 0.65f, scrollY, 0.9f, true);

  updateAndDrawMeteors();
}
