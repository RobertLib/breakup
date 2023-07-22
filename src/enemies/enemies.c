#include "enemies.h"
#include "../level-types.h"
#include "../level-manager.h"
#include "../lib/audio.h"
#include "../lib/camera.h"
#include "../lib/gfx.h"
#include "../lib/particles.h"
#include "../paddle/paddle.h"
#include "../bricks/brick.h"
#include "../ui/floating-text.h"

#define MAX_SPAWNERS 16
#define RESPAWN_DELAY 13.0f
#define ENEMY_BULLET_SPEED 230.0f
#define ENEMY_BULLET_SIZE 10

typedef struct Spawner
{
  Vec2 pos;
  EnemyType type;
  float timer; // counts down to the (re)spawn
  bool waiting;
} Spawner;

Enemy enemies[MAX_ENEMIES];
EnemyBullet enemyBullets[MAX_ENEMY_BULLETS];

static Spawner spawners[MAX_SPAWNERS];
static int numSpawners;

static SDL_Color enemyColor(EnemyType type)
{
  switch (type)
  {
  case ENEMY_DRIFTER:
    return (SDL_Color){255, 80, 200, 255};
  case ENEMY_DIVER:
    return (SDL_Color){255, 140, 50, 255};
  case ENEMY_SPLITTER:
    return (SDL_Color){150, 255, 90, 255};
  case ENEMY_SHOOTER:
    return (SDL_Color){160, 190, 255, 255};
  default:
    return (SDL_Color){255, 120, 220, 255};
  }
}

static Enemy *findInactiveEnemy(void)
{
  for (int i = 0; i < MAX_ENEMIES; i++)
  {
    if (!enemies[i].active)
    {
      return &enemies[i];
    }
  }

  return NULL;
}

static void spawnFromSpawner(int index)
{
  Spawner *spawner = &spawners[index];
  Enemy *enemy = findInactiveEnemy();

  if (enemy == NULL)
  {
    spawner->timer = 2.0f; // try again soon
    return;
  }

  initializeEnemy(enemy, spawner->type, spawner->pos.x, spawner->pos.y, index);
  spawner->waiting = false;

  spawnGlowPuff(spawner->pos.x + enemySize(spawner->type) / 2.0f,
                spawner->pos.y + enemySize(spawner->type) / 2.0f,
                enemyColor(spawner->type), 60, 0.4f);
}

// Takes the index rather than the enemy, because killEnemy() below cannot
// still trust enemy->spawnerIndex by the time it gets here.
static void startRespawn(int spawnerIndex)
{
  if (spawnerIndex >= 0 && spawnerIndex < numSpawners)
  {
    spawners[spawnerIndex].waiting = true;
    spawners[spawnerIndex].timer = RESPAWN_DELAY;
  }
}

void enemyDespawned(Enemy *enemy)
{
  startRespawn(enemy->spawnerIndex);
}

void killEnemy(Enemy *enemy, int score)
{
  if (!enemy->active)
  {
    return;
  }

  enemy->active = false;

  // Everything this function needs from the enemy is read here, before the
  // splitter branch below asks findInactiveEnemy() for somewhere to put its
  // minis - because the slot just freed on the line above is a candidate, and
  // usually the *first* candidate. The mini would then be initialized right on
  // top of this enemy with a spawnerIndex of -1, and the startRespawn() at the
  // bottom would find nothing to schedule: one killed splitter, one spawn point
  // gone for the rest of the level.
  const int spawnerIndex = enemy->spawnerIndex;
  const EnemyType type = enemy->type;

  int size = enemySize(type);
  float cx = enemy->pos.x + size / 2.0f;
  float cy = enemy->pos.y + size / 2.0f;

  SDL_Color color = enemyColor(type);

  playSfx(SFX_ENEMY_HIT);
  spawnBurst(cx, cy, color, 20, 260);
  spawnGlowPuff(cx, cy, color, 80, 0.35f);
  addTrauma(0.22f);
  addHitstop(0.045f);

  if (score > 0)
  {
    addScore(score);

    char text[16];
    snprintf(text, sizeof(text), "%d", score);
    spawnFloatingText(cx, cy, text, color);
  }

  // Splitters break into two minis
  if (type == ENEMY_SPLITTER)
  {
    for (int i = 0; i < 2; i++)
    {
      Enemy *mini = findInactiveEnemy();

      if (mini == NULL)
      {
        break;
      }

      initializeEnemy(mini, ENEMY_MINI,
                      cx - GFX_ENEMY_MINI_SIZE / 2.0f + (i == 0 ? -12 : 12),
                      cy - GFX_ENEMY_MINI_SIZE / 2.0f,
                      -1);
      mini->vel.x = i == 0 ? -0.7f : 0.7f;
      mini->vel.y = -0.7f;
      mini->vel = vec2Norm(mini->vel, 1);
    }
  }

  startRespawn(spawnerIndex);
}

void killEnemiesInRadius(float x, float y, float radius)
{
  for (int i = 0; i < MAX_ENEMIES; i++)
  {
    Enemy *enemy = &enemies[i];

    // Same guard as the ball's and the laser's: a blast that catches a
    // splitter must not also catch the minis it drops into the slots ahead of
    // this one, half a frame old and still sitting on top of it.
    if (!enemyIsTargetable(enemy))
    {
      continue;
    }

    int size = enemySize(enemy->type);
    float cx = enemy->pos.x + size / 2.0f;
    float cy = enemy->pos.y + size / 2.0f;
    float dx = cx - x;
    float dy = cy - y;

    if (dx * dx + dy * dy <= radius * radius)
    {
      killEnemy(enemy, 300);
    }
  }
}

bool spawnEnemyAt(EnemyType type, float x, float y)
{
  Enemy *enemy = findInactiveEnemy();

  if (enemy == NULL)
  {
    return false;
  }

  initializeEnemy(enemy, type, x, y, -1);
  spawnGlowPuff(x + enemySize(type) / 2.0f, y + enemySize(type) / 2.0f,
                enemyColor(type), 60, 0.4f);

  return true;
}

void spawnEnemyBullet(float x, float y)
{
  // Aim loosely toward the paddle
  float aim = clamp((paddle.pos.x - x) * 0.4f, -90.0f, 90.0f);

  spawnEnemyBulletAt(x, y, aim, ENEMY_BULLET_SPEED);
}

void spawnEnemyBulletAt(float x, float y, float vx, float vy)
{
  for (int i = 0; i < MAX_ENEMY_BULLETS; i++)
  {
    if (enemyBullets[i].active)
    {
      continue;
    }

    enemyBullets[i].pos = (Vec2){x - ENEMY_BULLET_SIZE / 2.0f, y};
    enemyBullets[i].vel = (Vec2){vx, vy};
    enemyBullets[i].active = true;

    return;
  }
}

void clearEnemyBullets(void)
{
  for (int i = 0; i < MAX_ENEMY_BULLETS; i++)
  {
    enemyBullets[i].active = false;
  }
}

static EnemyType typeForChar(char c)
{
  switch (c)
  {
  case 'V':
    return ENEMY_DIVER;
  case 'W':
    return ENEMY_SPLITTER;
  case 'U':
    return ENEMY_SHOOTER;
  default:
    return ENEMY_DRIFTER;
  }
}

void initializeEnemies(void)
{
  numSpawners = 0;

  for (int i = 0; i < MAX_ENEMIES; i++)
  {
    enemies[i].active = false;
  }

  clearEnemyBullets();

  const Level *currentLevel = getLevel(paddle.level);

  int x = 0;
  int y = 0;

  for (int i = 0; i < LEVEL_PATTERN_LENGTH; i++)
  {
    char c = currentLevel->pattern[i];

    if ((c == 'E' || c == 'V' || c == 'W' || c == 'U') && numSpawners < MAX_SPAWNERS)
    {
      spawners[numSpawners] = (Spawner){
          {LEVEL_PATTERN_INDENT + x * (BRICK_WIDTH + LEVEL_PATTERN_SPACING),
           LEVEL_PATTERN_TOP + y * (BRICK_HEIGHT + LEVEL_PATTERN_SPACING)},
          typeForChar(c),
          1.0f + numSpawners * 2.2f,
          true};
      numSpawners++;
    }

    x++;

    if (x >= LEVEL_PATTERN_COLS)
    {
      x = 0;
      y++;
    }
  }
}

static void updateEnemyBullets(void)
{
  for (int i = 0; i < MAX_ENEMY_BULLETS; i++)
  {
    EnemyBullet *b = &enemyBullets[i];

    if (!b->active)
    {
      continue;
    }

    b->pos.x += b->vel.x * dt;
    b->pos.y += b->vel.y * dt;

    // Off any edge, not just the bottom one. A saucer only ever fired
    // downward, so falling off the floor was the only way out a bullet needed;
    // a boss firing radially sends them up and sideways too, and those would
    // have flown off into the middle distance still holding a slot in the pool.
    if (b->pos.y > camera.y + SCREEN_HEIGHT ||
        b->pos.y + ENEMY_BULLET_SIZE < camera.y - 40 ||
        b->pos.x + ENEMY_BULLET_SIZE < -40 ||
        b->pos.x > SCREEN_WIDTH + 40)
    {
      b->active = false;
      continue;
    }

    // Hitting the paddle shrinks it
    if (checkCollision(
            b->pos.x, b->pos.y, ENEMY_BULLET_SIZE, ENEMY_BULLET_SIZE,
            paddle.pos.x - paddleWidth() / 2.0f, paddle.pos.y,
            paddleWidth(), PADDLE_HEIGHT))
    {
      b->active = false;

      if (paddle.type != PADDLE_TYPE_DYING)
      {
        changePaddleType(PADDLE_TYPE_SHORT);
        playSfx(SFX_POWERUP_BAD);
        spawnBurst(b->pos.x, b->pos.y, (SDL_Color){255, 100, 130, 255}, 12, 200);
        spawnFloatingText(paddle.pos.x, paddle.pos.y - 20, "SHRINK!",
                          (SDL_Color){255, 120, 100, 255});
        addTrauma(0.45f);
        addHitstop(0.07f);
      }
    }
  }
}

void updateEnemies(void)
{
  for (int i = 0; i < numSpawners; i++)
  {
    if (spawners[i].waiting)
    {
      spawners[i].timer -= (float)dt;

      if (spawners[i].timer <= 0)
      {
        spawnFromSpawner(i);
      }
    }
  }

  for (int i = 0; i < MAX_ENEMIES; i++)
  {
    updateEnemy(&enemies[i]);
  }

  updateEnemyBullets();
}

void drawEnemies(void)
{
  for (int i = 0; i < MAX_ENEMIES; i++)
  {
    drawEnemy(&enemies[i]);
  }

  // Enemy bullets: menacing purple orbs
  for (int i = 0; i < MAX_ENEMY_BULLETS; i++)
  {
    EnemyBullet *b = &enemyBullets[i];

    if (!b->active)
    {
      continue;
    }

    float screenY = b->pos.y - camera.y;

    SDL_SetTextureColorMod(texGlow, 255, 90, 160);
    SDL_SetTextureAlphaMod(texGlow, 200);
    SDL_FRect glow = {b->pos.x - 9, screenY - 9, 28, 28};
    SDL_RenderTexture(renderer, texGlow, NULL, &glow);
    SDL_SetTextureColorMod(texGlow, 255, 255, 255);
    SDL_SetTextureAlphaMod(texGlow, 255);

    SDL_SetTextureColorMod(texBall, 255, 120, 180);
    SDL_FRect core = {b->pos.x, screenY, ENEMY_BULLET_SIZE, ENEMY_BULLET_SIZE};
    SDL_RenderTexture(renderer, texBall, NULL, &core);
    SDL_SetTextureColorMod(texBall, 255, 255, 255);
  }
}

void destroyEnemies(void)
{
  numSpawners = 0;

  for (int i = 0; i < MAX_ENEMIES; i++)
  {
    enemies[i].active = false;
  }

  clearEnemyBullets();
}
