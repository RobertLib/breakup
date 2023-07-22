#pragma once

#include "../globals.h"

// World-space particle system (drawn relative to the camera).

void clearParticles(void);

void updateParticles(void);

void drawParticles(void);

// Colored explosion: glowing sparks + tumbling shards, thrown evenly around
void spawnBurst(float x, float y, SDL_Color color, int count, float speed);

// The same burst, thrown along `dir` instead of evenly around. Debris that
// carries on the way the ball was going says which way the hit came from, where
// a ring of sparks says only that something happened here. A zero `dir` falls
// back to the even spray.
void spawnImpactBurst(float x, float y, SDL_Color color, int count, float speed,
                      Vec2 dir);

// Single soft glow that expands and fades (impacts, muzzle flashes)
void spawnGlowPuff(float x, float y, SDL_Color color, float size, float life);

// Small trail dot behind the ball
void spawnTrail(float x, float y, SDL_Color color, float size);

// Big celebratory firework (win screen)
void spawnFirework(float x, float y);
