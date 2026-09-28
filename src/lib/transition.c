#include "transition.h"
#include "vector.h"
#include "../types.h"

static bool inProgress;
static float opacity;

static void (*transitionCallback)(void);

// realDt, not dt: a screen fade belongs to the screen rather than to the world,
// so it keeps running through a hit stop - the same rule the camera shake and
// the blinking prompts are on. A life lost freezes the world for 0.14s, and the
// fade out of a level should not freeze with it.
static void transitionTimer(void)
{
  if (inProgress)
  {
    opacity += realDt * 255 * 2.5;

    // Halfway point: switch the screen behind the fade
    if (opacity > 255 && transitionCallback != NULL)
    {
      transitionCallback();
      transitionCallback = NULL;
    }

    if (opacity > 255 * 2)
    {
      opacity = 0;
      inProgress = false;
    }
  }
}

bool isTransitionActive(void)
{
  return inProgress;
}

void startTransition(void (*callback)(void))
{
  if (inProgress)
  {
    return;
  }

  transitionCallback = callback;
  inProgress = true;
}

void initializeTransition(void)
{
  inProgress = false;
  opacity = 0;
  transitionCallback = NULL;
}

void updateTransition(void)
{
  transitionTimer();
}

void drawTransition(void)
{
  if (!inProgress)
  {
    return;
  }

  float alpha = opacity > 255 ? 255 * 2 - opacity : opacity;

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, (Uint8)clamp(alpha, 0, 255));

  SDL_FRect rect = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
  SDL_RenderFillRect(renderer, &rect);

  // A scan line down the screen on the way out and again on the way in: the
  // screens change the way the Sovereign's face draws itself in and out.
  float half = opacity > 255 ? (opacity - 255) / 255.0f : opacity / 255.0f;
  float y = clamp(half, 0.0f, 1.0f) * SCREEN_HEIGHT;
  float strength = sinf(clamp(half, 0.0f, 1.0f) * SDL_PI_F);
  SDL_Color glow = worldThemes[WORLD_COUNT].glow;

  vecLine(0, y, SCREEN_WIDTH, y, 6.0f, glow, 0.12f * strength);
  vecLine(0, y, SCREEN_WIDTH, y, 1.4f, mixColor(glow, (SDL_Color){255, 255, 255, 255}, 0.5f),
          0.85f * strength);
  vecFlush();
}

void destroyTransition(void)
{
}
