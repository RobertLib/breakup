#pragma once

#include "../bullet/bullet.h"
#include "../globals.h"

#define PADDLE_HEIGHT 20
#define NUMBER_OF_BULLETS 10

// How long a picked-up paddle type lasts before it reverts. Here rather than in
// paddle.c because the HUD draws the countdown bar and needs the same number to
// divide by; it had its own copy of the 8, which is two numbers that have to be
// edited together and only one of them anywhere near the timer.
#define PADDLE_TYPE_DURATION 8.0f

typedef enum PaddleType
{
  PADDLE_TYPE_DEFAULT,
  PADDLE_TYPE_LONG,
  PADDLE_TYPE_SHOOTING,
  PADDLE_TYPE_STICKY,
  PADDLE_TYPE_SHORT,
  PADDLE_TYPE_DYING,
} PaddleType;

typedef struct Paddle
{
  Vec2 pos;
  Vec2 vel;
  float moveToX; // mouse target (-1 = none)
  int score;
  int lives;
  int level;
  int levelBonus; // bonus awarded on level completion (for the banner)
  bool nextLevel;
  bool isReady;
  bool isTimeAfterStart;
  PaddleType type;
  Bullet bullets[NUMBER_OF_BULLETS];
  bool canShoot;
  bool inCollision;
  bool levelCompleted;
} Paddle;

extern Paddle paddle;

void addScore(const int value);

void setStartLevel(int level);

// The score the next initializePaddle() starts from, for BREAKUP_SCORE. Like
// setStartLevel() it is consumed once, so a restart after it begins at zero.
void setStartScore(int score);

void changePaddleType(PaddleType type);

// Seconds until a temporary paddle type reverts (0 when default)
float paddleTypeTimeLeft(void);

void paddleNextLevelUpdate(void);

void paddleTimerUpdate(void);

int paddleWidth(void);

// Trigger the impact squash & stretch animation (ball bounce, item pickup)
void paddleImpactKick(void);

void initializePaddle(void);

void updatePaddle(void);

void drawPaddle(void);

void destroyPaddle(void);
