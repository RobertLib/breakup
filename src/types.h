#pragma once

#include <SDL3/SDL.h>

// Power-up item types dropped by bricks
typedef enum ItemType
{
  ITEM_LONG,   // E — expand paddle
  ITEM_SHOOT,  // L — laser paddle
  ITEM_STICKY, // C — catch paddle
  ITEM_LIFE,   // P — extra life
  ITEM_MULTI,  // D — multiball
  ITEM_SLOW,   // S — slow balls
  ITEM_FIRE,   // F — fireball
  ITEM_SHIELD, // B — barrier at the bottom
  ITEM_SHRINK, // ! — shrink paddle (negative)
  ITEM_COUNT
} ItemType;

// Enemy behaviors
typedef enum EnemyType
{
  ENEMY_DRIFTER,  // wanders around, bounces off bricks
  ENEMY_DIVER,    // hovers, then dives toward the paddle
  ENEMY_SPLITTER, // splits into two minis when destroyed
  ENEMY_SHOOTER,  // saucer that shoots at the paddle
  ENEMY_MINI,     // small fast drifter (spawned by splitter)
  ENEMY_TYPE_COUNT
} EnemyType;

// Visual theme of a world (5 levels each)
typedef struct WorldTheme
{
  SDL_Color deep;   // nebula base color
  SDL_Color accent; // nebula highlight color
  SDL_Color glow;   // UI accent / glow color
  const char *name;
} WorldTheme;

#define WORLD_COUNT 4
#define LEVELS_PER_WORLD 5

extern const WorldTheme worldThemes[WORLD_COUNT + 1]; // +1 = menu theme

// Returns 0..WORLD_COUNT-1 for a 0-based level index. Implemented in
// level-manager.c, which is the only place that knows which levels are boss
// fights and therefore do not count toward a world's five.
int worldForLevel(int level);
