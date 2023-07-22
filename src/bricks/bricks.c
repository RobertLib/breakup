#include "bricks.h"
#include "brick-item.h"
#include "../boss/boss.h"
#include "../balls/balls.h"
#include "../enemies/enemies.h"
#include "../level-types.h"
#include "../level-manager.h"
#include "../lib/audio.h"
#include "../lib/camera.h"
#include "../lib/effects.h"
#include "../lib/particles.h"
#include "../lib/save.h"
#include "../paddle/paddle.h"
#include "../ui/floating-text.h"
#include "../utils/list.h"

#define CELL_WIDTH 60
#define CELL_HEIGHT 30
#define GRID_WIDTH (SCREEN_WIDTH / CELL_WIDTH)
#define GRID_HEIGHT (LEVEL_HEIGHT / CELL_HEIGHT)

// The level-intro pop-in wave, in seconds: when the first brick appears and
// how long the whole wave takes from there. The sum plus the per-column stagger
// has to stay under the 2s "GET READY" (readyTimer in paddle.c), which is when
// the player can first launch a ball.
#define BRICK_WAVE_START 0.15f
#define BRICK_WAVE_DURATION 1.4f

#define MAX_PENDING_EXPLOSIONS 48
#define EXPLOSION_RADIUS 88.0f
#define EXPLOSION_FUSE 0.14f
#define ITEM_DROP_CHANCE 0.21f

int numBricks;

Brick *bricks;

static List *grid[GRID_WIDTH][GRID_HEIGHT];

static bool levelHasFinal;

typedef struct PendingExplosion
{
  float x, y;
  float timer;
  bool active;
} PendingExplosion;

static PendingExplosion pendingExplosions[MAX_PENDING_EXPLOSIONS];

static BrickKind kindForChar(char c)
{
  switch (c)
  {
  case 'B':
    return BRICK_BASIC;
  case 'D':
    return BRICK_DURABLE;
  case 'T':
    return BRICK_TOUGH;
  case 'S':
    return BRICK_SOLID;
  case 'X':
    return BRICK_EXPLOSIVE;
  case 'G':
    return BRICK_GOLD;
  case 'F':
    return BRICK_FINAL;
  default:
    return BRICK_BASIC;
  }
}

static bool isBrickChar(char c)
{
  return c == 'B' || c == 'D' || c == 'T' || c == 'S' ||
         c == 'X' || c == 'G' || c == 'F';
}

static void assignBricksToGrid(void)
{
  for (int i = 0; i < numBricks; i++)
  {
    int gridX = bricks[i].pos.x / CELL_WIDTH;
    int gridY = bricks[i].pos.y / CELL_HEIGHT;

    if (gridX < 0 || gridX >= GRID_WIDTH || gridY < 0 || gridY >= GRID_HEIGHT)
    {
      fprintf(stderr, "Brick position out of grid bounds: (%d, %d)\n", gridX, gridY);
      continue;
    }

    if (!grid[gridX][gridY])
    {
      grid[gridX][gridY] = newList();

      if (!grid[gridX][gridY])
      {
        fprintf(stderr, "Failed to create new list for grid cell (%d, %d).\n", gridX, gridY);
        continue;
      }
    }

    listAdd(grid[gridX][gridY], &bricks[i]);
  }
}

int queryBricksNear(float x, float y, Brick **out, int maxOut)
{
  int gridX = (int)(x / CELL_WIDTH);
  int gridY = (int)(y / CELL_HEIGHT);
  int count = 0;

  for (int dx = -1; dx <= 1; dx++)
  {
    for (int dy = -1; dy <= 1; dy++)
    {
      int checkX = gridX + dx;
      int checkY = gridY + dy;

      if (checkX < 0 || checkX >= GRID_WIDTH || checkY < 0 || checkY >= GRID_HEIGHT)
      {
        continue;
      }

      List *cell = grid[checkX][checkY];

      if (cell == NULL)
      {
        continue;
      }

      for (Node *node = cell->head; node != NULL; node = node->next)
      {
        Brick *brick = (Brick *)node->data;

        // `spawnT > 0` is drawBrick()'s own test for whether a brick has
        // appeared yet: what cannot be seen does not deflect a ball, a laser
        // or an enemy. The wave finishes before the ball can be launched, so
        // this is a backstop rather than the fix - but a hitch during the
        // intro should not put an invisible wall in front of anybody.
        if (brick->active && brick->spawnT > 0 && count < maxOut)
        {
          out[count++] = brick;
        }
      }
    }
  }

  return count;
}

bool bricksOverlapRect(float x, float y, int w, int h)
{
  // assignBricksToGrid() files a brick by its top-left corner alone, and a
  // brick is wider and taller than nothing - so one whose corner sits in the
  // cell before this rectangle can still reach into it. The search therefore
  // begins a brick's width and height ahead of the rectangle's own edge, which
  // is the same allowance the 3x3 neighbourhood in queryBricksNear() above
  // makes by being three cells wide rather than one.
  int minX = (int)floorf((x - BRICK_WIDTH) / CELL_WIDTH);
  int maxX = (int)floorf((x + w) / CELL_WIDTH);
  int minY = (int)floorf((y - BRICK_HEIGHT) / CELL_HEIGHT);
  int maxY = (int)floorf((y + h) / CELL_HEIGHT);

  // Entirely off the grid, which is where the paddle spends every ordinary
  // level: it is parked a hundred pixels below the last row of bricks. Clamping
  // instead would search the nearest edge of the grid and could answer for a
  // brick that is nowhere near.
  if (maxX < 0 || minX >= GRID_WIDTH || maxY < 0 || minY >= GRID_HEIGHT)
  {
    return false;
  }

  minX = clamp(minX, 0, GRID_WIDTH - 1);
  maxX = clamp(maxX, 0, GRID_WIDTH - 1);
  minY = clamp(minY, 0, GRID_HEIGHT - 1);
  maxY = clamp(maxY, 0, GRID_HEIGHT - 1);

  for (int gx = minX; gx <= maxX; gx++)
  {
    for (int gy = minY; gy <= maxY; gy++)
    {
      List *cell = grid[gx][gy];

      if (cell == NULL)
      {
        continue;
      }

      for (Node *node = cell->head; node != NULL; node = node->next)
      {
        const Brick *brick = (const Brick *)node->data;

        // `active` and nothing else, which is deliberately not the test
        // queryBricksNear() applies. That one also asks whether a brick has
        // popped in yet, because what cannot be seen should not deflect a
        // ball. This answers a different question - whether the paddle is
        // buried - and a brick that is about to appear on top of it is every
        // bit as much in the way as one that already has.
        if (brick->active &&
            checkCollision(x, y, w, h,
                           brick->pos.x, brick->pos.y,
                           BRICK_WIDTH, BRICK_HEIGHT))
        {
          return true;
        }
      }
    }
  }

  return false;
}

void checkLevelComplete(void)
{
  if (paddle.levelCompleted)
  {
    return;
  }

  // On a boss level the bricks are the arena, not the objective: clearing them
  // ends nothing while the thing in the middle of them is still up.
  if (bossBlocksLevelEnd())
  {
    return;
  }

  // A life on its way out is not lost until lifeLossTimer says so. Completing
  // here - an explosion chain finishing the level after the last ball dropped -
  // would freeze the paddle mid-death and pay the bonus on a life count that
  // was about to go down. The life loss runs first and asks again after.
  if (paddle.type == PADDLE_TYPE_DYING)
  {
    return;
  }

  for (int i = 0; i < numBricks; i++)
  {
    if (!bricks[i].active)
    {
      continue;
    }

    if (levelHasFinal)
    {
      if (bricks[i].kind == BRICK_FINAL)
      {
        return;
      }
    }
    else if (bricks[i].kind != BRICK_SOLID)
    {
      return;
    }
  }

  paddle.levelCompleted = true;
  paddle.levelBonus = paddle.lives * 500;
  paddle.score += paddle.levelBonus;

  playSfx(SFX_LEVEL_COMPLETE);
  unlockLevel(paddle.level);
}

static void queueExplosion(float x, float y)
{
  for (int i = 0; i < MAX_PENDING_EXPLOSIONS; i++)
  {
    if (!pendingExplosions[i].active)
    {
      pendingExplosions[i] = (PendingExplosion){x, y, EXPLOSION_FUSE, true};
      return;
    }
  }
}

// The multiplier stepping up is the one moment in a chain worth saying out
// loud: it is where the player's score per brick actually changes, and it is
// the only part of the combo that is a decision rather than an accident.
static void announceCombo(float x, float y, int mult)
{
  static const SDL_Color tiers[4] = {
      {255, 230, 120, 255}, // x2
      {255, 170, 70, 255},  // x3
      {255, 110, 160, 255}, // x4
      {255, 255, 255, 255}, // x5, and the top of the ladder
  };

  SDL_Color color = tiers[clamp(mult - 2, 0, 3)];

  char label[24];
  snprintf(label, sizeof(label), "COMBO x%d", mult);

  spawnFloatingText(x, y - 30, label, color);
  spawnGlowPuff(x, y, color, 80.0f + 18.0f * mult, 0.45f);

  // A push rather than a shake: the frame leaning in says "that counted"
  // without costing the player sight of a ball that is still in play.
  addPunch(0.18f + 0.09f * mult);
  addFlash(color, 0.12f + 0.07f * mult);

  playSfxAt(SFX_GOLD, (mult - 1) * 2.0f);
}

// Where in the rising scale a break falls: one semitone per unbroken link, up
// an octave and then held, so a long chain climbs without ever going shrill.
static float comboSemitones(bool byBall)
{
  if (!byBall)
  {
    return 0;
  }

  return (float)clamp(combo - 1, 0, 12);
}

void damageBrick(Brick *brick, int damage, bool byBall, Vec2 impactDir)
{
  if (brick == NULL || !brick->active)
  {
    return;
  }

  float cx = brick->pos.x + BRICK_WIDTH / 2.0f;
  float cy = brick->pos.y + BRICK_HEIGHT / 2.0f;

  if (brick->kind == BRICK_SOLID)
  {
    spawnGlowPuff(cx, cy, (SDL_Color){200, 220, 255, 255}, 30, 0.2f);
    playSfx(SFX_BALL_WALL);
    return;
  }

  brick->hp -= damage;

  if (brick->hp > 0)
  {
    brick->hitFlash = 1.0f;
    playSfx(SFX_BRICK_HIT);
    spawnGlowPuff(cx, cy, brick->tint, 34, 0.22f);
    spawnImpactBurst(cx, cy, brick->tint, 4, 130, impactDir);
    return;
  }

  // Destroyed
  brick->active = false;
  spawnDyingBrick(brick);

  int mult = byBall ? comboMultiplier() : 1;
  int points = brick->value * mult;

  addScore(points);

  if (byBall)
  {
    registerBallBrickBreak();

    if (comboMultiplier() > mult)
    {
      announceCombo(cx, cy, comboMultiplier());
    }
  }

  char text[16];
  snprintf(text, sizeof(text), "%d", points);
  spawnFloatingText(cx, cy - 6, text, (SDL_Color){255, 255, 255, 255});

  SDL_Color burstColor = brick->tint;
  if (brick->kind == BRICK_GOLD)
  {
    burstColor = (SDL_Color){255, 210, 80, 255};
  }

  switch (brick->kind)
  {
  case BRICK_GOLD:
    playSfxAt(SFX_GOLD, comboSemitones(byBall));
    spawnImpactBurst(cx, cy, burstColor, 18, 240, impactDir);
    break;
  case BRICK_FINAL:
    playSfx(SFX_CRYSTAL);
    spawnBurst(cx, cy, burstColor, 26, 280);
    spawnGlowPuff(cx, cy, burstColor, 110, 0.5f);
    addTrauma(0.5f);
    addPunch(0.55f);
    addHitstop(0.1f);
    addFlash(burstColor, 0.55f);
    break;
  case BRICK_EXPLOSIVE:
    queueExplosion(cx, cy);
    spawnBurst(cx, cy, burstColor, 10, 180);
    break;
  default:
    playSfxAt(SFX_BRICK_BREAK, comboSemitones(byBall));
    spawnImpactBurst(cx, cy, burstColor, 14, 220, impactDir);
    break;
  }

  // Power-up drop
  if (brick->kind != BRICK_FINAL && brick->kind != BRICK_EXPLOSIVE &&
      frand() < ITEM_DROP_CHANCE)
  {
    trySpawnBrickItem(cx - GFX_ITEM_W / 2.0f, brick->pos.y);
  }

  checkLevelComplete();
}

static void detonate(float x, float y)
{
  addTrauma(0.65f);
  addPunch(0.6f);
  addHitstop(0.075f);
  addFlash((SDL_Color){255, 180, 90, 255}, 0.65f);
  playSfx(SFX_EXPLOSION);
  spawnBurst(x, y, (SDL_Color){255, 160, 60, 255}, 30, 330);
  spawnGlowPuff(x, y, (SDL_Color){255, 190, 90, 255}, 190, 0.45f);

  for (int i = 0; i < numBricks; i++)
  {
    Brick *b = &bricks[i];

    if (!b->active)
    {
      continue;
    }

    float bx = b->pos.x + BRICK_WIDTH / 2.0f;
    float by = b->pos.y + BRICK_HEIGHT / 2.0f;
    float dx = bx - x;
    float dy = by - y;

    if (dx * dx + dy * dy <= EXPLOSION_RADIUS * EXPLOSION_RADIUS)
    {
      damageBrick(b, 3, false, vec2Norm((Vec2){dx, dy}, 1.0f));
    }
  }

  killEnemiesInRadius(x, y, EXPLOSION_RADIUS);
  damageBossArmorAt(x, y, EXPLOSION_RADIUS);
}

void initializeBricks(void)
{
  // Whatever the last level left behind. destroyBricks() is NULL-safe, so a
  // first call costs nothing and a re-initialisation cannot leak.
  destroyBricks();

  levelHasFinal = false;

  clearDyingBricks();

  for (int i = 0; i < GRID_WIDTH; i++)
  {
    for (int j = 0; j < GRID_HEIGHT; j++)
    {
      grid[i][j] = NULL;
    }
  }

  for (int i = 0; i < MAX_PENDING_EXPLOSIONS; i++)
  {
    pendingExplosions[i].active = false;
  }

  const Level *currentLevel = getLevel(paddle.level);

  int lastBrickRow = 0;

  for (int i = 0; i < LEVEL_PATTERN_LENGTH; i++)
  {
    if (isBrickChar(currentLevel->pattern[i]))
    {
      numBricks++;
      lastBrickRow = i / LEVEL_PATTERN_COLS;
    }
  }

  bricks = (Brick *)malloc(sizeof(Brick) * (numBricks > 0 ? numBricks : 1));

  if (bricks == NULL)
  {
    fprintf(stderr, "Memory not allocated.\n");
    exit(1);
  }

  int brickIndex = 0;
  int x = 0;
  int y = 0;

  for (int i = 0; i < LEVEL_PATTERN_LENGTH; i++)
  {
    char c = currentLevel->pattern[i];

    if (isBrickChar(c))
    {
      BrickKind kind = kindForChar(c);

      if (kind == BRICK_FINAL)
      {
        levelHasFinal = true;
      }

      initializeBrick(
          &bricks[brickIndex],
          LEVEL_PATTERN_INDENT + x * (BRICK_WIDTH + LEVEL_PATTERN_SPACING),
          LEVEL_PATTERN_TOP + y * (BRICK_HEIGHT + LEVEL_PATTERN_SPACING),
          kind,
          y);

      // Level-intro wave: bricks pop in row by row, left to right, starting
      // from the bottom of the level and climbing.
      //
      // Both halves of that are about where the player is looking. The camera
      // starts at the *bottom* of the level content, so a wave that ran from
      // row 0 down arrived at the visible rows last; and at a fixed 0.05s per
      // row it took as long as the level was tall - 3.5s on level 20 against
      // a "GET READY" that clears at 2s. Which left the player looking at an
      // empty field, free to launch into rows that had not appeared yet and
      // were solid all the same. The wave is a fixed length now however tall
      // the level is, and it finishes before the ball can be launched.
      bricks[brickIndex].spawnDelay =
          BRICK_WAVE_START +
          (float)(lastBrickRow - y) / (float)(lastBrickRow > 0 ? lastBrickRow : 1) *
              BRICK_WAVE_DURATION +
          x * 0.014f;

      brickIndex++;
    }

    x++;

    if (x >= LEVEL_PATTERN_COLS)
    {
      x = 0;
      y++;
    }
  }

  assignBricksToGrid();
}

void updateBricks(void)
{
  // Advance the pop-in and hit-flash animations
  for (int i = 0; i < numBricks; i++)
  {
    Brick *b = &bricks[i];

    if (!b->active)
    {
      continue;
    }

    if (b->spawnDelay > 0)
    {
      b->spawnDelay -= (float)dt;
    }
    else if (b->spawnT < 1.0f)
    {
      b->spawnT = fminf(1.0f, b->spawnT + 3.5f * (float)dt);
    }

    if (b->hitFlash > 0)
    {
      b->hitFlash = fmaxf(0.0f, b->hitFlash - 4.0f * (float)dt);
    }
  }

  updateDyingBricks();

  for (int i = 0; i < MAX_PENDING_EXPLOSIONS; i++)
  {
    if (!pendingExplosions[i].active)
    {
      continue;
    }

    pendingExplosions[i].timer -= (float)dt;

    if (pendingExplosions[i].timer <= 0)
    {
      pendingExplosions[i].active = false;
      detonate(pendingExplosions[i].x, pendingExplosions[i].y);
    }
  }
}

void drawBricks(void)
{
  drawDyingBricks();

  for (int i = 0; i < numBricks; i++)
  {
    drawBrick(&bricks[i]);
  }
}

static void freeGrid(void)
{
  for (int i = 0; i < GRID_WIDTH; i++)
  {
    for (int j = 0; j < GRID_HEIGHT; j++)
    {
      if (grid[i][j] != NULL)
      {
        freeList(grid[i][j]);
        grid[i][j] = NULL;
      }
    }
  }
}

void destroyBricks(void)
{
  free(bricks);
  bricks = NULL;
  numBricks = 0;

  freeGrid();
}
