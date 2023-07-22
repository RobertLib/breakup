#include "balls.h"
#include "../lib/audio.h"
#include "../lib/camera.h"
#include "../lib/effects.h"
#include "../paddle/paddle.h"

Ball balls[BALL_COUNT];

int combo;

int comboMultiplier(void)
{
  int mult = 1 + combo / 5;
  return mult > 5 ? 5 : mult;
}

void registerBallBrickBreak(void)
{
  combo++;
}

void resetCombo(void)
{
  combo = 0;
}

void activateAllBalls(void)
{
  // New balls launch from the position of the first live ball
  Ball *source = NULL;

  for (int i = 0; i < BALL_COUNT; i++)
  {
    if (balls[i].active && !balls[i].docked)
    {
      source = &balls[i];
      break;
    }
  }

  for (int i = 0; i < BALL_COUNT; i++)
  {
    if (balls[i].active)
    {
      continue;
    }

    if (source != NULL)
    {
      balls[i].active = true;
      balls[i].docked = false;
      balls[i].pos = source->pos;
      balls[i].speed = source->speed;
      balls[i].trailTimer = 0;

      // Collapse the motion streak onto the new position, the same way
      // releaseBall() does and for the same reason. A ball is only inactive
      // here because it fell off the bottom of the field, and its history[]
      // still holds the way down: without this, a multiball drew ten frames of
      // afterimages stretching from the new ball back to wherever the old one
      // was lost.
      for (int h = 0; h < BALL_HISTORY; h++)
      {
        balls[i].history[h] = balls[i].pos;
      }
      balls[i].historyHead = 0;

      float angle = frandRange(-0.9f, 0.9f);
      balls[i].vel.x = sinf(angle) * source->speed;
      balls[i].vel.y = -fabsf(cosf(angle)) * source->speed;
    }
    else
    {
      initializeBall(&balls[i], true);
    }
  }
}

void resetBalls(void)
{
  resetCombo();

  for (int i = 0; i < BALL_COUNT; i++)
  {
    initializeBall(&balls[i], i == 0);
  }
}

bool isBallsDocked(void)
{
  bool allDocked = true;

  for (int i = 0; i < BALL_COUNT; i++)
  {
    if (!isBallDocked(&balls[i]))
    {
      allDocked = false;
      break;
    }
  }

  return allDocked;
}

void releaseBalls(void)
{
  for (int i = 0; i < BALL_COUNT; i++)
  {
    releaseBall(&balls[i]);
  }
}

void initializeBalls(void)
{
  resetBalls();
}

static bool isOffScreen(const Ball *ball)
{
  return ball->pos.x + BALL_SIZE < 0 ||
         ball->pos.x > SCREEN_WIDTH ||
         ball->pos.y > camera.y + SCREEN_HEIGHT;
}

void updateBalls(void)
{
  for (int i = 0; i < BALL_COUNT; i++)
  {
    updateBall(&balls[i]);
  }

  // Camera scroll: climbing pushes the view upward. One request per frame - the
  // strongest one - rather than one per ball. Every ball used to apply its own
  // from inside updateBall(), so a multiball level scrolled at two and three
  // times the speed a single ball has ever moved the view, and the paddle went
  // with it.
  float pull = 0;

  for (int i = 0; i < BALL_COUNT; i++)
  {
    float ask = ballCameraPull(&balls[i]);

    if (ask < pull)
    {
      pull = ask;
    }
  }

  if (pull < 0)
  {
    camera.y += pull;
    paddle.pos.y += pull;
  }

  // After the scroll, because the bottom edge this measures against has just
  // moved.
  bool areAllActiveBallsOffScreen = true;

  for (int i = 0; i < BALL_COUNT; i++)
  {
    if (balls[i].active)
    {
      if (isOffScreen(&balls[i]))
      {
        balls[i].active = false;
      }
      else
      {
        areAllActiveBallsOffScreen = false;
      }
    }
  }

  if (areAllActiveBallsOffScreen && paddle.type != PADDLE_TYPE_DYING)
  {
    paddle.type = PADDLE_TYPE_DYING;
    playSfx(SFX_LIFE_LOST);

    // The longest stop in the game, on the one event the player most needs to
    // feel: the world holds still for a beat before the paddle starts dying.
    addTrauma(0.9f);
    addHitstop(0.14f);
    resetCombo();
    resetEffects();
  }
}

void drawBalls(void)
{
  for (int i = 0; i < BALL_COUNT; i++)
  {
    drawBall(&balls[i]);
  }
}

void destroyBalls(void)
{
}
