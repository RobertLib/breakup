#pragma once

#include "ball.h"
#include "../globals.h"

#define BALL_COUNT 3

extern Ball balls[BALL_COUNT];

// Combo: consecutive brick breaks without touching the paddle
extern int combo;

// 1 + a step for every five links, up to the cap (x5, or x8 with GLASS
// CANNON), and one more on top while the paddle is shrunk.
int comboMultiplier(void);

// Adds `links` to the chain and, when that moves the multiplier, announces it
// at (x, y) in world space. Returns whether it did.
bool addComboLinks(int links, float x, float y);

// Launches one more ball from the first in flight, if there is a free slot.
void spawnExtraBall(void);

void resetCombo(void);

void activateAllBalls(void);

void resetBalls(void);

bool isBallsDocked(void);

void releaseBalls(void);

void initializeBalls(void);

void updateBalls(void);

void drawBalls(void);

void destroyBalls(void);
