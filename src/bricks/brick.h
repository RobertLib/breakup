#pragma once

#include "../globals.h"

static const int BRICK_WIDTH = 50;

extern const int BRICK_HEIGHT;

typedef enum BrickKind
{
  BRICK_BASIC,     // 1 hit
  BRICK_DURABLE,   // 2 hits
  BRICK_TOUGH,     // 3 hits
  BRICK_SOLID,     // indestructible
  BRICK_EXPLOSIVE, // blows up its neighborhood
  BRICK_GOLD,      // bonus points
  BRICK_FINAL      // level objective crystal
} BrickKind;

typedef struct Brick
{
  Vec2 pos;
  BrickKind kind;
  int hp;
  int maxHp;
  int value;
  SDL_Color tint;
  bool active;
  float spawnDelay; // seconds until the level-intro pop-in starts
  float spawnT;     // pop-in progress 0..1
  float hitFlash;   // 1..0 white flash + squash after a non-lethal hit
} Brick;

// Sets up hp, score value and tint based on kind and pattern row
void initializeBrick(Brick *brick, float x, float y, BrickKind kind, int row);

void drawBrick(const Brick *brick);

// Short spin-and-fade animation played where a brick was destroyed
void spawnDyingBrick(const Brick *brick);

void updateDyingBricks(void);

void drawDyingBricks(void);

void clearDyingBricks(void);
