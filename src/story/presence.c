#include "presence.h"
#include "being.h"
#include "../balls/balls.h"
#include "../boss/boss.h"
#include "../level-manager.h"
#include "../lib/camera.h"
#include "../lib/gfx.h"
#include "../lib/save.h"
#include "../paddle/paddle.h"
#include "../run/run.h"
#include "../types.h"

// The fight the Sovereign is the core of.
#define SOVEREIGN_BOSS 7

// Where the face in the back of a level hangs, and how big: most of the width
// of the brick pattern, which is what it is behind.
#define WATCHER_X (SCREEN_WIDTH / 2.0f)
#define WATCHER_Y 250.0f
#define WATCHER_SCALE 120.0f

// Faint. The lines are there to be noticed on the second look; the eyes are
// there to be noticed on the first.
#define WATCHER_ALPHA 0.13f
#define WATCHER_EYES 3.0f

// Small enough to sit inside the crystal it is the face of.
#define CORE_SCALE 24.0f

typedef enum Mode
{
  MODE_NONE,
  MODE_WATCHER, // in the back of an ordinary level
  MODE_CORE     // on The Sovereign's crystal
} Mode;

static Mode mode;
static Being face;
static float damage; // what the run has cost it so far
static float hurt;   // 1 just after a crystal, falling away
static float moodT;  // how long the reaction's mood lasts yet
static StoryMood reaction;

void presenceBegin(void)
{
  mode = MODE_NONE;
  hurt = 0;
  moodT = 0;

  if (!saveData.story)
  {
    return;
  }

  const Level *level = getLevel(paddle.level);

  if (level->boss == SOVEREIGN_BOSS)
  {
    mode = MODE_CORE;
  }
  else if (level->boss == 0)
  {
    mode = MODE_WATCHER;
  }
  else
  {
    // Its wardens fight for it, and the fight is theirs to be looked at.
    return;
  }

  damage = storyDamage(runAct(), runWon());

  beingReset(&face);
  beingSetMood(&face, STORY_MOOD_CALM);

  if (mode == MODE_WATCHER)
  {
    beingPlace(&face, WATCHER_X, WATCHER_Y);
    face.scale = WATCHER_SCALE;
    face.eyeGlow = WATCHER_EYES;
    face.wander = 1.6f;
  }
  else
  {
    beingPlace(&face, boss.pos.x, boss.pos.y - camera.y);
    face.scale = CORE_SCALE;
    face.halo = false;
    face.wander = 0;
    face.turn = 0.45f;
    face.grid = 0.45f;
    face.eyeGlow = 1.3f;
  }
}

void presenceReact(PresenceEvent event)
{
  if (mode == MODE_NONE)
  {
    return;
  }

  switch (event)
  {
  case PRESENCE_CRYSTAL:
  case PRESENCE_CORE_HIT:
    // A thought gone, or its own heart struck: it flinches, gasps, and for a
    // moment its face does not hold together.
    reaction = STORY_MOOD_PAINED;
    moodT = 0.9f;
    hurt = 1.0f;
    beingSpeak(&face, 1.0f);
    break;

  case PRESENCE_LIFE_LOST:
    reaction = STORY_MOOD_AMUSED;
    moodT = 2.6f;
    break;

  case PRESENCE_CLEARED:
    reaction = STORY_MOOD_STERN;
    moodT = 4.0f;
    break;
  }
}

// What it watches: the ball nearest the bottom, which is the one that matters,
// or the paddle while there is none in flight.
static SDL_FPoint watched(void)
{
  SDL_FPoint at = {paddle.pos.x, paddle.pos.y - camera.y};
  float lowest = -1e9f;

  for (int i = 0; i < BALL_COUNT; i++)
  {
    if (balls[i].active && !balls[i].docked && balls[i].pos.y > lowest)
    {
      lowest = balls[i].pos.y;
      at = (SDL_FPoint){balls[i].pos.x + BALL_SIZE / 2.0f,
                        balls[i].pos.y + BALL_SIZE / 2.0f - camera.y};
    }
  }

  return at;
}

void updatePresence(void)
{
  if (mode == MODE_NONE)
  {
    return;
  }

  // The world's clock: a flinch waits out the hit stop of the crystal it is
  // flinching at, and a paused game is a face holding still.
  float step = (float)dt;

  hurt = fmaxf(0, hurt - step * 1.6f);
  moodT -= step;

  StoryMood mood = moodT > 0 ? reaction : STORY_MOOD_CALM;

  if (mode == MODE_CORE)
  {
    // Shut, the crystal's eyes are half closed; struck, they are wide.
    if (moodT <= 0 && boss.invuln > 0)
    {
      mood = STORY_MOOD_SOFT;
    }

    beingPlace(&face, boss.pos.x, boss.pos.y - camera.y);

    // The killing blow takes the face with the crystal.
    if (!boss.alive && !face.shattered)
    {
      beingShatter(&face);
    }
  }

  beingSetMood(&face, mood);
  beingSetGlitch(&face, damage + hurt * 0.55f);

  SDL_FPoint at = watched();
  beingLookAt(&face, at.x, at.y);

  updateBeing(&face, step);
}

void drawPresence(void)
{
  if (mode != MODE_WATCHER)
  {
    return;
  }

  int world = clamp(worldForLevel(paddle.level), 0, WORLD_COUNT - 1);

  drawBeing(&face, worldThemes[world].glow, WATCHER_ALPHA);
}

void drawPresenceOnCore(void)
{
  if (mode != MODE_CORE)
  {
    return;
  }

  // A window into the crystal to see it through. Lines are light, and light
  // drawn over something as bright as the core does not show - so the middle
  // of it is darkened first, softly, the way the glow texture falls off.
  if (!face.shattered)
  {
    float size = CORE_SCALE * 3.4f;
    float x = boss.pos.x;
    float y = boss.pos.y - camera.y;
    SDL_FRect dst = {x - size / 2, y - size / 2, size, size};

    SDL_SetTextureBlendMode(texGlow, SDL_BLENDMODE_BLEND);
    SDL_SetTextureColorMod(texGlow, 14, 6, 30);
    SDL_SetTextureAlphaMod(texGlow, 235);
    SDL_RenderTexture(renderer, texGlow, NULL, &dst);
    SDL_SetTextureBlendMode(texGlow, SDL_BLENDMODE_ADD);
    SDL_SetTextureColorMod(texGlow, 255, 255, 255);
    SDL_SetTextureAlphaMod(texGlow, 255);
  }

  // Dimmed with the crystal while it is shut, and white-hot while it flashes.
  float shut = boss.invuln > 0 && boss.alive ? 0.55f : 1.0f;
  SDL_Color tint = mixColor(worldThemes[WORLD_COUNT - 1].glow, (SDL_Color){255, 255, 255, 255},
                            0.35f + 0.6f * boss.coreFlash);

  drawBeing(&face, tint, 0.95f * shut);
}
