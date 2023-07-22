#pragma once

#include "../globals.h"
#include "../lib/gfx.h"
#include "../types.h"

// Falling power-up capsules.

void resetBrickItems(void);

// Spawns a random capsule at (x, y); no-op if the pool is full
void trySpawnBrickItem(float x, float y);

void updateBrickItems(void);

void drawBrickItems(void);
