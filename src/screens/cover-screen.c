#include "cover-screen.h"
#include "../bricks/brick.h"
#include "../level-types.h"
#include "../lib/camera.h"
#include "../lib/game-state.h"
#include "../lib/gfx.h"
#include "../lib/particles.h"
#include "../lib/postfx.h"
#include "../lib/starfield.h"
#include "../lib/transition.h"
#include "../types.h"

// The itch.io cover, which is the one picture of this game most people will
// ever see: at 630x500 on its own page and at 315x250 in a grid of other covers,
// every one of them painted to be read at exactly that size.
//
// A crop of a screenshot cannot do that job, and it was tried. Play at any one
// frame is a ball the size of a pea in front of a tidy wall, the game's name is
// nowhere in it, and nothing about it says that anything is happening. So this
// is a staged frame instead - the instant a fireball goes through the wall, with
// the title over it - drawn with the sprites, particles and bloom the game plays
// with, so that it is still a picture of this game and is still rebuilt by
// `make press` whenever the art changes.
//
// It is a set, not a level. Nothing here collides or scores: the wall is laid
// out once, the blast is thrown once, and from then on the sparks and the debris
// only fly - so the press kit picks the moment by picking the frame.

#define TITLE "BREAKUP"
#define TITLE_LEN ((int)(sizeof(TITLE) - 1))

// How wide the name runs, lean included. The press kit keeps the middle 756 of
// the 800 (630x500 is a little narrower than 4:3), so this leaves a margin of
// about thirty either side - and at 315x250 a name very nearly as wide as the
// cover, which is the size it has to be read at.
#define TITLE_WIDTH 690.0f
#define TITLE_TOP 26.0f
#define TITLE_GAP 9.0f
#define TITLE_LEAN 0.2f // forward lean, as a fraction of the letters' height
#define TITLE_DEPTH 10  // extrusion steps behind the face

#define WALL_ROWS 9
#define WALL_TOP 232.0f
#define WALL_HUE 290.0f

// The hole the ball has just made, as the half-axes of an ellipse around the
// point of impact. Its edge is roughened per brick (see initializeCoverScreen).
#define HOLE_RX 92.0f
#define HOLE_RY 50.0f

// How the wall's light falls off with distance from the blast: to half at
// LIGHT_REACH, and never below LIGHT_FLOOR at the far edges.
#define LIGHT_REACH 210.0f
#define LIGHT_FLOOR 0.36f

// How far past the point of impact the ball has got: out of the top of the wall
// and clear of the worst of the blast, so that it is a ball against the dark and
// not a dark spot in the middle of something white.
#define BALL_AHEAD 112.0f

#define MAX_DEBRIS 96
#define NUM_RAYS 22
#define NUM_EMBERS 60
#define RING_SEGMENTS 72

// The wall, as a level file would spell it (see level-types.h), so that it can
// be read and changed as one.
static const char *const wallPattern[WALL_ROWS] = {
    "BBBBBBBBBBBBBBB",
    "BBGBBBBBBBBBGBB",
    "SBBBBXBBBBBBBBS",
    "BBBBBBBBBXBBBBB",
    "BBDDBBBBBBBDDBB",
    "BBBBBBBBBBBBBBB",
    "SBBXBBBBBBBBXBS",
    "BBBBBBBBBBBBBBB",
    "BBBBBGBBBGBBBBB",
};

// A brick that has left the wall. The game's own dying bricks are a quiet
// spin-and-shrink where one broke; these are the same sprites thrown the way a
// blast throws them, and the few thrown hardest are drawn larger and last of
// all, as near enough the camera to pass in front of everything else.
typedef struct Debris
{
  Vec2 pos; // centre
  Vec2 vel;
  float rot;
  float rotVel;
  float scale;
  SDL_Color tint;
  BrickKind kind;
  bool front;
} Debris;

typedef struct Ray
{
  float angle;
  float halfWidth;
  float length;
  float alpha;
} Ray;

typedef struct Ember
{
  Vec2 pos;
  float size;
  SDL_Color color;
  Uint8 alpha;
} Ember;

static Brick wall[WALL_ROWS * LEVEL_PATTERN_COLS];
static int numWall;

static Debris debris[MAX_DEBRIS];
static int numDebris;

static Ray rays[NUM_RAYS];
static Ember embers[NUM_EMBERS];

static TTF_Font *titleFont;
static SDL_Texture *letters[TITLE_LEN];
static SDL_Texture *tagline;

// Where the shot came from, where it hit, which way it was going and where the
// ball is now.
static const Vec2 launch = {330, 552};
static const Vec2 impact = {462, 300};
static Vec2 heading;
static Vec2 ball;

static void leave(void)
{
  nextGameState = GAME_STATE_INTRO_SCREEN;
}

static float distance(Vec2 a, Vec2 b)
{
  return sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

static float distanceToSegment(Vec2 p, Vec2 a, Vec2 b)
{
  float dx = b.x - a.x;
  float dy = b.y - a.y;
  float t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / (dx * dx + dy * dy);
  t = clamp(t, 0.0f, 1.0f);

  return distance(p, (Vec2){a.x + dx * t, a.y + dy * t});
}

static SDL_Color mix(SDL_Color a, SDL_Color b, float t)
{
  return (SDL_Color){
      (Uint8)lerp(a.r, b.r, t),
      (Uint8)lerp(a.g, b.g, t),
      (Uint8)lerp(a.b, b.b, t),
      255};
}

static SDL_FColor fcolor(SDL_Color c, float alpha)
{
  return (SDL_FColor){c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, alpha};
}

static BrickKind kindFor(char c)
{
  switch (c)
  {
  case 'S':
    return BRICK_SOLID;
  case 'X':
    return BRICK_EXPLOSIVE;
  case 'G':
    return BRICK_GOLD;
  case 'D':
    return BRICK_DURABLE;
  default:
    return BRICK_BASIC;
  }
}

static SDL_Texture *textureFor(BrickKind kind)
{
  switch (kind)
  {
  case BRICK_SOLID:
    return texBrickSolid;
  case BRICK_GOLD:
    return texBrickGold;
  default:
    return texBrick;
  }
}

static Vec2 brickCentre(const Brick *brick)
{
  return (Vec2){brick->pos.x + BRICK_WIDTH / 2.0f,
                brick->pos.y + BRICK_HEIGHT / 2.0f};
}

// One sprite centred on a point, tinted, and put back the way it was found.
static void drawSprite(SDL_Texture *tex, Vec2 at, float w, float h,
                       SDL_Color color, Uint8 alpha, float angle)
{
  SDL_SetTextureColorMod(tex, color.r, color.g, color.b);
  SDL_SetTextureAlphaMod(tex, alpha);
  SDL_FRect dst = {at.x - w / 2, at.y - h / 2, w, h};
  SDL_RenderTextureRotated(renderer, tex, NULL, &dst, angle, NULL, SDL_FLIP_NONE);
  SDL_SetTextureColorMod(tex, 255, 255, 255);
  SDL_SetTextureAlphaMod(tex, 255);
}

static void addDebris(const Brick *brick, Vec2 vel, float scale, bool front)
{
  if (numDebris >= MAX_DEBRIS)
  {
    return;
  }

  Debris *d = &debris[numDebris++];
  d->pos = brickCentre(brick);
  d->vel = vel;
  d->rot = frandRange(-30, 30);
  d->rotVel = frandRange(-760, 760);
  d->scale = scale;
  d->tint = brick->tint;
  d->kind = brick->kind;
  d->front = front;
}

// Out from the blast, and on along the way the ball was going: a hole punched
// from below spits its wall upward rather than evenly round.
static void throwDebris(const Brick *brick, float scale)
{
  Vec2 from = brickCentre(brick);
  Vec2 away = vec2Norm((Vec2){from.x - impact.x, from.y - impact.y}, 1.0f);
  float push = frandRange(280, 720);
  float carry = frandRange(140, 360);

  addDebris(brick,
            (Vec2){away.x * push + heading.x * carry, away.y * push + heading.y * carry},
            scale, false);
}

void initializeCoverScreen(void)
{
  // Everything drawn here is in world space, as the field is, with the camera
  // at the bottom of a level that has not been scrolled.
  clearParticles();
  camera.y = 0;

  heading = vec2Norm((Vec2){impact.x - launch.x, impact.y - launch.y}, 1.0f);
  ball = (Vec2){impact.x + heading.x * BALL_AHEAD, impact.y + heading.y * BALL_AHEAD};

  // The wall, and the hole in it.
  numWall = 0;
  numDebris = 0;

  for (int row = 0; row < WALL_ROWS; row++)
  {
    for (int col = 0; col < LEVEL_PATTERN_COLS; col++)
    {
      Brick *brick = &wall[numWall];
      float x = LEVEL_PATTERN_INDENT + col * (BRICK_WIDTH + LEVEL_PATTERN_SPACING);
      float y = WALL_TOP + row * (BRICK_HEIGHT + LEVEL_PATTERN_SPACING);

      initializeBrick(brick, x, y, kindFor(wallPattern[row][col]), row);
      brick->spawnT = 1.0f;

      if (brick->kind == BRICK_BASIC)
      {
        brick->tint = hsvColor(WALL_HUE + row * 41.0f, 0.72f, 1.0f);
      }

      Vec2 c = brickCentre(brick);
      float ex = (c.x - impact.x) / HOLE_RX;
      float ey = (c.y - impact.y) / HOLE_RY;
      float reach = sqrtf(ex * ex + ey * ey);

      // Roughened, so that the hole is a hole and not a stencil of an ellipse
      float edge = frandRange(0.8f, 1.25f);

      // ...and the tunnel the ball cut on its way through
      bool tunnel = distanceToSegment(c, launch, ball) < BRICK_WIDTH * 0.6f;

      if (brick->kind != BRICK_SOLID && (reach < edge || tunnel))
      {
        throwDebris(brick, frandRange(0.75f, 1.05f));

        if (numDebris % 3 == 0)
        {
          throwDebris(brick, frandRange(0.38f, 0.55f));
        }

        Vec2 away = vec2Norm((Vec2){c.x - impact.x, c.y - impact.y}, 1.0f);
        spawnImpactBurst(c.x, c.y, brick->tint, 10, 360, away);
        continue;
      }

      // The blast is the light in this picture, so the wall falls off into
      // the dark away from it. Evenly lit, fifteen columns of neon compete with
      // the one thing on the cover that is happening.
      float d = distance(c, impact) / LIGHT_REACH;
      float light = lerp(LIGHT_FLOOR, 1.0f, 1.0f / (1.0f + d * d));

      brick->tint = (SDL_Color){(Uint8)(brick->tint.r * light),
                                (Uint8)(brick->tint.g * light),
                                (Uint8)(brick->tint.b * light), 255};

      // What is left standing near the blast is lit by it, jolted by it, and
      // at the rim of the hole cracked through.
      float heat = clamp((2.3f - reach) / 1.3f, 0.0f, 1.0f);

      if (heat > 0)
      {
        brick->tint = mix(brick->tint, (SDL_Color){255, 214, 160, 255}, heat * 0.3f);
        brick->hitFlash = heat * 0.45f;

        if (heat > 0.55f && brick->kind != BRICK_SOLID && brick->kind != BRICK_GOLD)
        {
          brick->maxHp = 3;
          brick->hp = 1;
        }
      }

      numWall++;
    }
  }

  // The pieces nearest the camera: thrown hardest, drawn larger, and aimed -
  // out to the sides and under the name, which is the one thing on the cover
  // that must not be hidden.
  static const struct
  {
    float angle; // degrees, 0 = right, -90 = straight up
    float speed;
    float scale;
    int row;
  } near[] = {
      {-162, 900, 1.45f, 1},
      {-24, 860, 1.35f, 3},
      {-118, 560, 1.2f, 0},
      {14, 700, 1.25f, 5},
  };

  for (size_t i = 0; i < sizeof(near) / sizeof(near[0]); i++)
  {
    Brick piece;
    initializeBrick(&piece, impact.x - BRICK_WIDTH / 2.0f,
                    impact.y - BRICK_HEIGHT / 2.0f, BRICK_BASIC, near[i].row);
    piece.tint = hsvColor(WALL_HUE + near[i].row * 41.0f, 0.72f, 1.0f);

    float a = near[i].angle * SDL_PI_F / 180.0f;
    addDebris(&piece, (Vec2){cosf(a) * near[i].speed, sinf(a) * near[i].speed},
              near[i].scale, true);
  }

  // The blast itself: the game's own bursts, thrown at the same moment.
  spawnImpactBurst(impact.x, impact.y, (SDL_Color){255, 150, 60, 255}, 90, 620, heading);
  spawnBurst(impact.x, impact.y, (SDL_Color){255, 236, 200, 255}, 50, 440);
  spawnGlowPuff(impact.x, impact.y, (SDL_Color){255, 180, 110, 255}, 200, 0.8f);
  spawnGlowPuff(impact.x, impact.y, (SDL_Color){255, 250, 235, 255}, 120, 0.5f);

  // Embers shed along the way in. Held here rather than handed to the particle
  // system, whose trail dots last a third of a second and would be gone by the
  // frame the cover is taken at.
  for (int i = 0; i < NUM_EMBERS; i++)
  {
    float t = sqrtf(frand());
    float side = frandRange(-16, 16) * (0.4f + t);

    embers[i].pos = (Vec2){lerp(launch.x, impact.x, t) - heading.y * side,
                           lerp(launch.y, impact.y, t) + heading.x * side};
    embers[i].size = frandRange(5, 14) * (0.5f + t);
    embers[i].color = mix((SDL_Color){255, 90, 30, 255},
                          (SDL_Color){255, 220, 140, 255}, frand());
    embers[i].alpha = (Uint8)frandRange(120, 240);
  }

  for (int i = 0; i < NUM_RAYS; i++)
  {
    rays[i].angle = frand() * 2.0f * SDL_PI_F;
    rays[i].halfWidth = frandRange(0.012f, 0.05f);
    rays[i].length = frandRange(260, 620);
    rays[i].alpha = frandRange(0.10f, 0.30f);
  }

  // The name. Its own size, rather than font64 stretched: at twice the logical
  // frame, which is what the cover is captured at, anything drawn larger than it
  // was rasterized is visibly soft.
  titleFont = loadFont("assets/font.ttf", 96);

  for (int i = 0; i < TITLE_LEN; i++)
  {
    char letter[2] = {TITLE[i], '\0'};
    letters[i] = renderTextBlended(titleFont, letter, (SDL_Color){255, 255, 255, 255});
  }

  tagline = renderTextBlended(font16, "NEON  BRICK-BREAKER  ROGUELITE",
                              (SDL_Color){255, 255, 255, 255});
}

void updateCoverScreen(void)
{
  updateParticles();

  for (int i = 0; i < numDebris; i++)
  {
    Debris *d = &debris[i];

    d->vel.y += 420.0f * (float)dt;
    d->pos.x += d->vel.x * (float)dt;
    d->pos.y += d->vel.y * (float)dt;
    d->rot += d->rotVel * (float)dt;
  }

  // Not a screen anybody is meant to arrive at, but one they can leave.
  if ((anyKeyPressed || isMousePressed[1]) && !isTransitionActive())
  {
    startTransition(leave);
  }
}

// ---------------------------------------------------------------------------
// Drawing, back to front
// ---------------------------------------------------------------------------

// Shafts of light out of the blast, under everything else.
static void drawRays(void)
{
  const SDL_Color warm = {255, 196, 140, 255};

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);

  for (int i = 0; i < NUM_RAYS; i++)
  {
    const Ray *r = &rays[i];
    float a0 = r->angle - r->halfWidth;
    float a1 = r->angle + r->halfWidth;

    SDL_Vertex v[3] = {
        {{impact.x, impact.y}, fcolor(warm, r->alpha), {0, 0}},
        {{impact.x + cosf(a0) * r->length, impact.y + sinf(a0) * r->length},
         fcolor(warm, 0),
         {0, 0}},
        {{impact.x + cosf(a1) * r->length, impact.y + sinf(a1) * r->length},
         fcolor(warm, 0),
         {0, 0}},
    };

    SDL_RenderGeometry(renderer, NULL, v, 3, NULL, 0);
  }

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
}

// A pressure ring, bright in the middle of its width and gone at both edges.
static void drawRing(float radius, float width, SDL_Color color, float alpha)
{
  SDL_Vertex v[(RING_SEGMENTS + 1) * 3];
  int idx[RING_SEGMENTS * 12];

  for (int s = 0; s <= RING_SEGMENTS; s++)
  {
    float a = s * 2.0f * SDL_PI_F / RING_SEGMENTS;
    float cx = cosf(a);
    float cy = sinf(a);
    float radii[3] = {radius - width, radius, radius + width};

    for (int k = 0; k < 3; k++)
    {
      v[s * 3 + k] = (SDL_Vertex){
          {impact.x + cx * radii[k], impact.y + cy * radii[k]},
          fcolor(color, k == 1 ? alpha : 0),
          {0, 0}};
    }
  }

  for (int s = 0; s < RING_SEGMENTS; s++)
  {
    int a = s * 3;
    int b = (s + 1) * 3;
    int quads[2][4] = {{a, a + 1, b + 1, b}, {a + 1, a + 2, b + 2, b + 1}};

    for (int q = 0; q < 2; q++)
    {
      int *out = &idx[s * 12 + q * 6];
      out[0] = quads[q][0];
      out[1] = quads[q][1];
      out[2] = quads[q][2];
      out[3] = quads[q][0];
      out[4] = quads[q][2];
      out[5] = quads[q][3];
    }
  }

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
  SDL_RenderGeometry(renderer, NULL, v, (RING_SEGMENTS + 1) * 3, idx, RING_SEGMENTS * 12);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
}

static void drawWall(void)
{
  for (int i = 0; i < numWall; i++)
  {
    drawBrick(&wall[i]);
  }
}

// The comet the shot leaves behind it: a run of glows along the line from the
// paddle, cooler and thinner the further back.
static void drawTrail(void)
{
  const int steps = 70;

  for (int i = 0; i < steps; i++)
  {
    float t = (i + 1) / (float)steps;
    Vec2 p = {lerp(launch.x, ball.x, t), lerp(launch.y, ball.y, t)};
    float size = 16 + 56 * t * t;

    drawSprite(texGlow, p, size, size,
               mix((SDL_Color){255, 60, 20, 255}, (SDL_Color){255, 190, 100, 255}, t),
               (Uint8)(60 + 150 * t * t), 0);
  }

  for (int i = 0; i < NUM_EMBERS; i++)
  {
    drawSprite(texGlow, embers[i].pos, embers[i].size, embers[i].size,
               embers[i].color, embers[i].alpha, 0);
  }

  // A white-hot core down the last half of it
  for (int i = 0; i < steps / 2; i++)
  {
    float t = 0.5f + (i + 1) / (float)steps;
    Vec2 p = {lerp(launch.x, ball.x, t), lerp(launch.y, ball.y, t)};
    float size = 8 + 18 * (t - 0.5f) * 2;

    drawSprite(texGlow, p, size, size, (SDL_Color){255, 246, 225, 255},
               (Uint8)(200 * (t - 0.5f) * 2), 0);
  }
}

static void drawBlast(void)
{
  drawSprite(texGlow, impact, 560, 560, (SDL_Color){255, 90, 40, 255}, 110, 0);
  drawSprite(texGlow, impact, 260, 260, (SDL_Color){255, 170, 90, 255}, 170, 0);
  drawSprite(texGlow, impact, 110, 110, (SDL_Color){255, 240, 220, 255}, 200, 0);
  drawSprite(texSparkle, impact, 360, 360, (SDL_Color){255, 232, 200, 255}, 220, 8);
  drawSprite(texSparkle, impact, 210, 210, (SDL_Color){255, 190, 140, 255}, 140, 53);
}

static void drawOneDebris(const Debris *d)
{
  SDL_Texture *tex = textureFor(d->kind);
  float w = BRICK_WIDTH * d->scale;
  float h = BRICK_HEIGHT * d->scale;

  // The neon it carries with it
  drawSprite(texGlow, d->pos, 70 * d->scale, 70 * d->scale, d->tint, 70, 0);

  // Motion blur: fainter copies back along the way it came
  for (int g = 4; g >= 1; g--)
  {
    float back = 0.011f * g;
    Vec2 p = {d->pos.x - d->vel.x * back, d->pos.y - d->vel.y * back};
    drawSprite(tex, p, w, h, d->tint, (Uint8)(90 / g), d->rot - d->rotVel * back);
  }

  drawSprite(tex, d->pos, w, h, d->tint, 255, d->rot);

  if (d->kind == BRICK_EXPLOSIVE)
  {
    drawSprite(texBombEmblem, d->pos, w, h, (SDL_Color){255, 255, 255, 255}, 255,
               d->rot);
  }
}

static void drawDebris(bool front)
{
  for (int i = 0; i < numDebris; i++)
  {
    if (debris[i].front == front)
    {
      drawOneDebris(&debris[i]);
    }
  }
}

static void drawFireball(void)
{
  float angle = atan2f(heading.y, heading.x) * 57.2958f;
  Vec2 at = ball;

  // Afterimages, as drawBall() leaves them, only more of them
  for (int i = 0; i < 9; i++)
  {
    float back = (9 - i) * 9.0f;
    float t = (i + 1) / 10.0f;
    Vec2 p = {at.x - heading.x * back, at.y - heading.y * back};
    float size = 28 * (0.4f + 0.6f * t);

    drawSprite(texBall, p, size, size, (SDL_Color){255, 150, 60, 255},
               (Uint8)(130 * t * t), 0);
  }

  // A comet's tail: glows drawn long along the heading, streaming off the back
  for (int i = 0; i < 3; i++)
  {
    float back = 22.0f + i * 20.0f;
    Vec2 p = {at.x - heading.x * back, at.y - heading.y * back};

    drawSprite(texGlow, p, 130 - i * 24, 44 - i * 8,
               mix((SDL_Color){255, 210, 120, 255}, (SDL_Color){255, 90, 30, 255}, i / 2.0f),
               (Uint8)(210 - i * 50), angle);
  }

  drawSprite(texGlow, at, 92, 92, (SDL_Color){255, 150, 60, 255}, 200, 0);
  drawSprite(texBall, at, 38, 30, (SDL_Color){255, 200, 130, 255}, 255, angle);

  // And the same ball again, additively: the sprite is shaded to sit on a dark
  // field, and in the middle of this much light its shaded rim reads as a dark
  // ring around a hole. White-hot is what a fireball is at this moment.
  SDL_SetTextureBlendMode(texBall, SDL_BLENDMODE_ADD);
  drawSprite(texBall, at, 38, 30, (SDL_Color){255, 230, 190, 255}, 255, angle);
  SDL_SetTextureBlendMode(texBall, SDL_BLENDMODE_BLEND);
  drawSprite(texGlow, at, 34, 30, (SDL_Color){255, 255, 245, 255}, 255, angle);
}

// A capsule on its way down out of the wall, in the corner the shot left empty.
static void drawCapsule(ItemType type, Vec2 at, float angle)
{
  const float w = GFX_ITEM_W * 1.3f;
  const float h = GFX_ITEM_H * 1.3f;
  SDL_Color glow = itemColor(type);

  for (int g = 3; g >= 1; g--)
  {
    drawSprite(texItem[type], (Vec2){at.x, at.y - g * 7.0f}, w, h,
               (SDL_Color){255, 255, 255, 255}, (Uint8)(36 / g), angle);
  }

  drawSprite(texGlow, at, w * 2.2f, h * 3.2f, glow, 150, 0);
  drawSprite(texItem[type], at, w, h, (SDL_Color){255, 255, 255, 255}, 255, angle);
}

static void drawPaddleArt(void)
{
  const SDL_Color neon = {90, 200, 255, 255};
  Vec2 at = {launch.x, launch.y + GFX_PADDLE_H / 2.0f + 4};

  drawSprite(texGlow, at, 260, 90, neon, 120, 0);
  drawSprite(texPaddle[1], at, 150, GFX_PADDLE_H, neon, 255, 0);
  drawSprite(texGlow, at, 170, 26, (SDL_Color){200, 240, 255, 255}, 90, 0);
}

// A letter as two quads rather than one, so that the colour can run through
// three stops top to bottom, and sheared so the name leans into the shot.
static void drawLetter(SDL_Texture *tex, float x, float y, float w, float h,
                       SDL_FColor top, SDL_FColor middle, SDL_FColor bottom)
{
  SDL_Vertex v[6];
  const SDL_FColor stops[3] = {top, middle, bottom};

  for (int row = 0; row < 3; row++)
  {
    float t = row / 2.0f;
    float left = x + TITLE_LEAN * h * (1.0f - t);
    float at = y + h * t;

    v[row * 2 + 0] = (SDL_Vertex){{left, at}, stops[row], {0, t}};
    v[row * 2 + 1] = (SDL_Vertex){{left + w, at}, stops[row], {1, t}};
  }

  static const int idx[12] = {0, 1, 3, 0, 3, 2, 2, 3, 5, 2, 5, 4};
  SDL_RenderGeometry(renderer, tex, v, 6, idx, 12);
}

static void drawLetterFlat(SDL_Texture *tex, float x, float y, float w, float h,
                           SDL_FColor color)
{
  drawLetter(tex, x, y, w, h, color, color, color);
}

static void drawTitle(void)
{
  float widths[TITLE_LEN];
  float height = getSize(letters[0]).y;
  float natural = TITLE_GAP * (TITLE_LEN - 1) + TITLE_LEAN * height;

  for (int i = 0; i < TITLE_LEN; i++)
  {
    widths[i] = getSize(letters[i]).x;
    natural += widths[i];
  }

  float scale = TITLE_WIDTH / natural;
  float h = height * scale;
  float x0 = SCREEN_WIDTH / 2.0f - TITLE_WIDTH / 2.0f;

  float xs[TITLE_LEN];
  float x = x0;

  for (int i = 0; i < TITLE_LEN; i++)
  {
    xs[i] = x;
    x += (widths[i] + TITLE_GAP) * scale;
  }

  // A pool of dark behind the name, so the blast and the stars do not show
  // through its counters. texGlow is additive; for this one pass it is not.
  SDL_SetTextureBlendMode(texGlow, SDL_BLENDMODE_BLEND);

  for (int i = 0; i < TITLE_LEN; i++)
  {
    Vec2 c = {xs[i] + widths[i] * scale / 2 + TITLE_LEAN * h / 2, TITLE_TOP + h / 2};
    drawSprite(texGlow, c, h * 2.4f, h * 2.0f, (SDL_Color){4, 2, 14, 255}, 190, 0);
  }

  SDL_SetTextureBlendMode(texGlow, SDL_BLENDMODE_ADD);

  // Neon haze, which the bloom then spreads further
  for (int i = 0; i < TITLE_LEN; i++)
  {
    Vec2 c = {xs[i] + widths[i] * scale / 2 + TITLE_LEAN * h / 2, TITLE_TOP + h / 2};
    drawSprite(texGlow, c, h * 1.9f, h * 1.7f, (SDL_Color){255, 60, 150, 255}, 120, 0);
  }

  // Extrusion: the letters stacked back and down into a deep violet
  for (int k = TITLE_DEPTH; k >= 1; k--)
  {
    float t = k / (float)TITLE_DEPTH;
    SDL_Color c = mix((SDL_Color){150, 40, 170, 255}, (SDL_Color){26, 8, 54, 255}, t);

    for (int i = 0; i < TITLE_LEN; i++)
    {
      drawLetterFlat(letters[i], xs[i] + k * 0.45f, TITLE_TOP + k * 1.0f,
                     widths[i] * scale, h, fcolor(c, 1));
    }
  }

  // A dark keyline round the face, which is what keeps its pink lower half
  // apart from the violet behind it at 315x250
  for (int k = 0; k < 8; k++)
  {
    float a = k * SDL_PI_F / 4.0f;

    for (int i = 0; i < TITLE_LEN; i++)
    {
      drawLetterFlat(letters[i], xs[i] + cosf(a) * 1.6f, TITLE_TOP + sinf(a) * 1.6f,
                     widths[i] * scale, h, fcolor((SDL_Color){22, 6, 40, 255}, 1));
    }
  }

  // The face: white-hot at the top, through the fireball's orange, to pink
  for (int i = 0; i < TITLE_LEN; i++)
  {
    drawLetter(letters[i], xs[i], TITLE_TOP, widths[i] * scale, h,
               fcolor((SDL_Color){255, 252, 228, 255}, 1),
               fcolor((SDL_Color){255, 184, 76, 255}, 1),
               fcolor((SDL_Color){255, 64, 128, 255}, 1));
  }

  // The line underneath it
  SDL_FPoint size = getSize(tagline);
  float ty = TITLE_TOP + h + 4;
  SDL_FRect shadow = {SCREEN_WIDTH / 2.0f - size.x / 2 + 2, ty + 2, size.x, size.y};
  SDL_FRect dst = {SCREEN_WIDTH / 2.0f - size.x / 2, ty, size.x, size.y};

  SDL_SetTextureColorMod(tagline, 0, 0, 0);
  SDL_SetTextureAlphaMod(tagline, 200);
  SDL_RenderTexture(renderer, tagline, NULL, &shadow);
  SDL_SetTextureColorMod(tagline, 150, 220, 255);
  SDL_SetTextureAlphaMod(tagline, 255);
  SDL_RenderTexture(renderer, tagline, NULL, &dst);
  SDL_SetTextureColorMod(tagline, 255, 255, 255);
}

void drawCoverScreen(void)
{
  // Bloom all the way up and a touch of the chromatic split the heaviest hits
  // get in play: this is the heaviest hit there is.
  PostFx fx = {.zoom = 1.0f, .bloom = 1.0f, .split = 0.35f};
  bool scene = beginScene();

  drawBackground(0, 3, 0);
  drawRays();
  drawWall();
  drawTrail();
  drawBlast();
  drawRing(150, 14, (SDL_Color){255, 226, 190, 255}, 0.45f);
  drawRing(214, 22, (SDL_Color){255, 170, 120, 255}, 0.16f);
  drawDebris(false);
  drawParticles();
  drawFireball();
  drawCapsule(ITEM_MULTI, (Vec2){632, 488}, 11);
  drawPaddleArt();
  drawTitle();
  drawDebris(true);

  if (scene)
  {
    endScene(&fx);
  }
}

void destroyCoverScreen(void)
{
  for (int i = 0; i < TITLE_LEN; i++)
  {
    SDL_DestroyTexture(letters[i]);
    letters[i] = NULL;
  }

  SDL_DestroyTexture(tagline);
  tagline = NULL;

  TTF_CloseFont(titleFont);
  titleFont = NULL;

  clearParticles();
}
