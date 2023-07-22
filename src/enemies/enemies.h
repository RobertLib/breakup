#pragma once

#include "enemy.h"
#include "../globals.h"

#define MAX_ENEMIES 24
// Enough for a boss that fires in every direction at once, rather than for a
// saucer that fires one at a time.
#define MAX_ENEMY_BULLETS 24

typedef struct EnemyBullet
{
  Vec2 pos;
  Vec2 vel;
  bool active;
} EnemyBullet;

extern Enemy enemies[MAX_ENEMIES];
extern EnemyBullet enemyBullets[MAX_ENEMY_BULLETS];

// Kills an enemy: explosion, score, splitter spawns minis, spawner respawn
void killEnemy(Enemy *enemy, int score);

// An enemy left the play area without dying (starts the spawner respawn timer)
void enemyDespawned(Enemy *enemy);

// Used by explosive bricks
void killEnemiesInRadius(float x, float y, float radius);

// Fired by shooter enemies, loosely aimed at the paddle.
//
// Neither of these plays a sound: the caller does, once per volley. A boss
// firing radially spawns up to ten of these on one frame, and ten copies of
// SFX_SHOOT in a frame is not ten times as loud, it is the whole ten-track
// sound pool spent on one event - every other sound in the game cut off mid-note
// to make room for nine more of the same click.
void spawnEnemyBullet(float x, float y);

// The same bullet with the aiming left to the caller, for the bosses that fan
// their shots out or fire in every direction at once.
void spawnEnemyBulletAt(float x, float y, float vx, float vy);

// Puts an enemy on the field that no spawn point owns, so nothing schedules a
// respawn when it dies. The boss summons its escorts this way.
// Returns false when the field is already full.
bool spawnEnemyAt(EnemyType type, float x, float y);

void clearEnemyBullets(void);

void initializeEnemies(void);

void updateEnemies(void);

void drawEnemies(void);

void destroyEnemies(void);
