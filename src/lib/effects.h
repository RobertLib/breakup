#pragma once

#include "../globals.h"

// Timed global gameplay effects (from power-ups).

#define EFFECT_FIRE_DURATION 8.0f
#define EFFECT_SLOW_DURATION 7.0f
#define EFFECT_SHIELD_DURATION 10.0f

typedef struct Effects
{
  float fire;   // fireball: balls smash through bricks
  float slow;   // balls move slower
  float shield; // barrier at the bottom of the screen

  // Visual only: brief full-screen additive flash (explosions, crystals)
  float flash;
  SDL_Color flashColor;
} Effects;

extern Effects effects;

// Kick off a screen flash; strength in [0, 1]
void addFlash(SDL_Color color, float strength);

void resetEffects(void);

void updateEffects(void);

bool fireballActive(void);

bool slowActive(void);

bool shieldActive(void);
