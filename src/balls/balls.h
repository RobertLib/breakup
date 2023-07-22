#pragma once

#include "ball.h"
#include "../globals.h"

#define BALL_COUNT 3

extern Ball balls[BALL_COUNT];

// Combo: consecutive brick breaks without touching the paddle
extern int combo;

int comboMultiplier(void);

void registerBallBrickBreak(void);

void resetCombo(void);

void activateAllBalls(void);

void resetBalls(void);

bool isBallsDocked(void);

void releaseBalls(void);

void initializeBalls(void);

void updateBalls(void);

void drawBalls(void);

void destroyBalls(void);
