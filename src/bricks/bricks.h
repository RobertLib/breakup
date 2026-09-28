#pragma once

#include "brick.h"
#include "../globals.h"

extern int numBricks;

extern Brick *bricks;

// Collects active bricks in the 3x3 grid cells around (x, y).
// Returns the number of bricks written to out (up to maxOut).
int queryBricksNear(float x, float y, Brick **out, int maxOut);

// Whether any active brick overlaps the rectangle. Asked once a frame by the
// paddle, which needs the answer rather than the bricks, and goes through the
// same spatial grid queryBricksNear() does rather than walking every brick in
// the level.
bool bricksOverlapRect(float x, float y, int w, int h);

// What landed a hit, which is what decides what the combo makes of it.
typedef enum HitSource
{
  HIT_BALL,      // scores at the multiplier and adds a link
  HIT_LASER,     // plain points, unless the run holds TRACER ROUNDS
  HIT_EXPLOSION, // scores at the multiplier; adds a link with CHAIN REACTION
  HIT_BOSS       // the arena going up with a boss: plain points
} HitSource;

// Applies damage to a brick, handling scoring, effects, drops, explosions
// and the level-complete check. Solid bricks shrug it off (unless a fireball
// under MELTDOWN is what hit them).
// impactDir: which way the blow was travelling, for the debris to follow;
// {0, 0} throws it evenly around instead.
void damageBrick(Brick *brick, int damage, HitSource source, Vec2 impactDir);

// Ends the level if nothing is left standing that would keep it open. Called
// after every brick that breaks, and by the boss when it finally goes down.
void checkLevelComplete(void);

void initializeBricks(void);

void updateBricks(void);

void drawBricks(void);

void destroyBricks(void);
