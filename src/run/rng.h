#pragma once

#include <SDL3/SDL_stdinc.h>

// A random number generator of the game's own, for the one thing that has to
// come out the same on every machine: the shape of a run.
//
// Everything else draws from rand(), and rand() is not one algorithm. macOS,
// glibc, mingw's msvcrt and emscripten's musl each ship their own, so the same
// seed is four different sequences - which is fine for a spark or a power-up
// drop and fatal for a daily run, whose whole point is that everybody who
// plays it on a given date gets the same levels, the same mutators and the
// same perks on offer. This is PCG32 (O'Neill, pcg-random.org): eight bytes of
// state plus a stream selector, and the reference implementation's output for
// a given seed is pinned by tests/test_run.c.
typedef struct Rng
{
  Uint64 state;
  Uint64 inc; // the stream; always odd
} Rng;

// `stream` picks one of 2^63 independent sequences for the same seed, which is
// how a run keeps its level order, its mutators and its perk offers apart: a
// change to how many numbers one of them draws cannot shift the others.
void rngSeed(Rng *rng, Uint64 seed, Uint64 stream);

Uint32 rngNext(Rng *rng);

// Uniform in [0, n), without the bias `% n` has. 0 for an n below 1.
int rngBelow(Rng *rng, int n);
