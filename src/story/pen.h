#pragma once

#include "../globals.h"

// What the Sovereign writes with: a small spinning crystal at the end of a beam
// from the eye in its forehead, throwing off sparks as each letter goes down.
// The story writes its pages with it and the title screen writes the name of
// the game; the stylus on its own is the cursor in every menu, pointing at
// the choice the way it points at the letter it is writing.

#define PEN_SPARKS 128

typedef struct PenSpark
{
  float x, y, vx, vy;
  float life, maxLife;
} PenSpark;

typedef struct Pen
{
  float x, y;
  float presence; // 0..1, how much of it is there
  float time;
  PenSpark sparks[PEN_SPARKS];
} Pen;

void penReset(Pen *pen, float x, float y);

// Follows (x, y): quickly while it is writing, lazily while it is not, and
// fades itself in or out to match.
void penFollow(Pen *pen, float x, float y, bool writing, float dt);

// A few sparks off a letter just written at (x, y).
void penSpark(Pen *pen, float x, float y, float speed);

void updatePen(Pen *pen, float dt);

// The beam from `from` (NULL for none), the stylus, and the sparks.
void drawPen(const Pen *pen, const SDL_FPoint *from, SDL_Color tint, float alpha);

// The stylus by itself at (x, y), `size` pixels from its middle to its tips.
// Collects into the vector batch; the caller flushes.
void drawStylus(float x, float y, float size, float spin, SDL_Color color, float alpha);
