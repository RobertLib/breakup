#pragma once

#include "../globals.h"

#define BALL_SIZE 20

// Recent center positions, drawn as a fading motion streak
#define BALL_HISTORY 10

typedef struct Ball
{
  Vec2 pos;
  Vec2 vel; // direction * speed
  float speed;
  bool active;
  bool docked;
  float dockOffsetX;  // offset from paddle center while docked
  float stickyTimer;  // auto-release countdown for sticky catches
  float trailTimer;
  Vec2 history[BALL_HISTORY];
  int historyHead;
} Ball;

bool isBallDocked(const Ball *ball);

void releaseBall(Ball *ball);

void initializeBall(Ball *ball, bool active);

void updateBall(Ball *ball);

// How far up this ball would like the view scrolled this frame (<= 0 world
// units), or 0 if it is not climbing into the top of the view.
//
// It is a question rather than an action because updateBalls() asks it of every
// ball and acts on the answer once. Scrolling from inside updateBall() meant
// three balls climbing together scrolled the level three times as fast as one.
float ballCameraPull(const Ball *ball);

void drawBall(const Ball *ball);
