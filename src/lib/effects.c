#include "effects.h"

Effects effects;

void resetEffects(void)
{
  effects.fire = 0;
  effects.slow = 0;
  effects.shield = 0;
  effects.flash = 0;
}

void updateEffects(void)
{
  effects.fire = fmaxf(0, effects.fire - (float)dt);
  effects.slow = fmaxf(0, effects.slow - (float)dt);
  effects.shield = fmaxf(0, effects.shield - (float)dt);
  effects.flash = fmaxf(0, effects.flash - 3.0f * (float)dt);
}

void addFlash(SDL_Color color, float strength)
{
  if (strength > effects.flash)
  {
    effects.flash = clamp(strength, 0.0f, 1.0f);
    effects.flashColor = color;
  }
}

bool fireballActive(void)
{
  return effects.fire > 0;
}

bool slowActive(void)
{
  return effects.slow > 0;
}

bool shieldActive(void)
{
  return effects.shield > 0;
}
