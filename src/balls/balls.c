#include "balls.h"
#include "../lib/audio.h"
#include "../lib/camera.h"
#include "../lib/effects.h"
#include "../lib/particles.h"
#include "../paddle/paddle.h"
#include "../run/perks.h"
#include "../run/run.h"
#include "../story/presence.h"
#include "../ui/floating-text.h"

Ball balls[BALL_COUNT];

int combo;

int comboMultiplier(void)
{
  int cap = runComboCap();
  int mult = 1 + combo / 5;

  if (mult > cap)
  {
    mult = cap;
  }

  // The Shrink capsule is a gamble rather than a plain punishment: a paddle
  // that small is worth one more on the multiplier for as long as it lasts, so
  // catching it is a choice a player can make on purpose. It sits on top of
  // the cap, because it is paid for with the paddle rather than with the chain.
  if (paddle.type == PADDLE_TYPE_SHORT)
  {
    mult++;
  }

  return mult;
}

// The multiplier stepping up is the one moment in a chain worth saying out
// loud: it is where the player's score per brick actually changes.
static void announceCombo(float x, float y, int mult)
{
  static const SDL_Color tiers[4] = {
      {255, 230, 120, 255}, // x2
      {255, 170, 70, 255},  // x3
      {255, 110, 160, 255}, // x4
      {255, 255, 255, 255}, // x5, and the top of the ladder
  };

  SDL_Color color = tiers[clamp(mult - 2, 0, 3)];

  char label[24];
  snprintf(label, sizeof(label), "COMBO x%d", mult);

  spawnFloatingText(x, y - 30, label, color);
  spawnGlowPuff(x, y, color, 80.0f + 18.0f * clamp(mult, 2, 5), 0.45f);

  // A push rather than a shake: the frame leaning in says "that counted"
  // without costing the player sight of a ball that is still in play.
  addPunch(0.18f + 0.09f * clamp(mult, 2, 5));
  addFlash(color, 0.12f + 0.07f * clamp(mult, 2, 5));

  playSfxAt(SFX_GOLD, (clamp(mult, 2, 8) - 1) * 2.0f);
}

bool addComboLinks(int links, float x, float y)
{
  int before = comboMultiplier();

  combo += links;

  int after = comboMultiplier();

  noteComboMultiplier(after);

  if (after <= before)
  {
    return false;
  }

  announceCombo(x, y, after);

  if (runHasPerk(PERK_SPLIT_SHOT))
  {
    spawnExtraBall();
  }

  return true;
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

// One more ball, from the first one in flight, the way a multiball launches
// its two. Nothing happens when every ball is already out or none is in play
// to launch it from - SPLIT SHOT is a bonus, not a second multiball capsule.
void spawnExtraBall(void)
{
  Ball *source = NULL;

  for (int i = 0; i < BALL_COUNT; i++)
  {
    if (balls[i].active && !balls[i].docked)
    {
      source = &balls[i];
      break;
    }
  }

  if (source == NULL)
  {
    return;
  }

  for (int i = 0; i < BALL_COUNT; i++)
  {
    if (balls[i].active)
    {
      continue;
    }

    balls[i].active = true;
    balls[i].docked = false;
    balls[i].pos = source->pos;
    balls[i].speed = source->speed;
    balls[i].trailTimer = 0;

    for (int h = 0; h < BALL_HISTORY; h++)
    {
      balls[i].history[h] = balls[i].pos;
    }
    balls[i].historyHead = 0;

    float angle = frandRange(-0.9f, 0.9f);
    balls[i].vel.x = sinf(angle) * source->speed;
    balls[i].vel.y = -fabsf(cosf(angle)) * source->speed;

    spawnGlowPuff(source->pos.x + BALL_SIZE / 2.0f,
                  source->pos.y + BALL_SIZE / 2.0f,
                  (SDL_Color){255, 120, 200, 255}, 60, 0.3f);
    return;
  }
}

// SAFETY NET: the last ball over the edge is thrown back up from the bottom of
// the screen instead, once per level, with the barrier's own bounce and flash
// so that it reads as the same kind of save.
static void rescueBall(Ball *ball)
{
  ball->pos.x = clamp(ball->pos.x, 0.0f, (float)(SCREEN_WIDTH - BALL_SIZE));
  ball->pos.y = camera.y + SCREEN_HEIGHT - BALL_SIZE - 16;
  ball->vel.y = -fabsf(ball->vel.y);

  if (fabsf(ball->vel.y) < ball->speed * 0.5f)
  {
    ball->vel = vec2Norm((Vec2){ball->vel.x, -ball->speed}, ball->speed);
  }

  for (int h = 0; h < BALL_HISTORY; h++)
  {
    ball->history[h] = ball->pos;
  }

  float cx = ball->pos.x + BALL_SIZE / 2.0f;
  float cy = camera.y + SCREEN_HEIGHT - 14;
  SDL_Color color = {110, 220, 255, 255};

  playSfx(SFX_SHIELD);
  spawnGlowPuff(cx, cy, color, 120, 0.4f);
  spawnFloatingText(cx, cy - 40, "SAFETY NET", color);
  addFlash(color, 0.35f);
  addPunch(0.3f);
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
  Ball *lastLost = NULL;

  for (int i = 0; i < BALL_COUNT; i++)
  {
    if (balls[i].active)
    {
      if (isOffScreen(&balls[i]))
      {
        balls[i].active = false;
        lastLost = &balls[i];
      }
      else
      {
        areAllActiveBallsOffScreen = false;
      }
    }
  }

  // Asked only when this frame lost the last ball, so the net is spent on the
  // ball that would have cost a life and not on one of three in a multiball.
  if (areAllActiveBallsOffScreen && lastLost != NULL &&
      paddle.type != PADDLE_TYPE_DYING && !paddle.levelCompleted &&
      runUseSafetyNet())
  {
    lastLost->active = true;
    rescueBall(lastLost);
    areAllActiveBallsOffScreen = false;
  }

  if (areAllActiveBallsOffScreen && paddle.type != PADDLE_TYPE_DYING)
  {
    paddle.type = PADDLE_TYPE_DYING;
    playSfx(SFX_LIFE_LOST);
    presenceReact(PRESENCE_LIFE_LOST);

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
