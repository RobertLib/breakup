#include "rng.h"

// pcg32_srandom_r and pcg32_random_r from the PCG reference implementation,
// line for line, so that a known seed produces the known sequence.
void rngSeed(Rng *rng, Uint64 seed, Uint64 stream)
{
  rng->state = 0;
  rng->inc = (stream << 1u) | 1u;
  rngNext(rng);
  rng->state += seed;
  rngNext(rng);
}

Uint32 rngNext(Rng *rng)
{
  Uint64 old = rng->state;

  rng->state = old * 6364136223846793005ULL + rng->inc;

  Uint32 xorshifted = (Uint32)(((old >> 18u) ^ old) >> 27u);
  Uint32 rot = (Uint32)(old >> 59u);

  return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
}

int rngBelow(Rng *rng, int n)
{
  if (n <= 1)
  {
    return 0;
  }

  // Rejects the few values at the bottom of the range that would make the
  // low results more likely than the high ones: 2^32 is not a multiple of n.
  Uint32 bound = (Uint32)n;
  Uint32 threshold = (0u - bound) % bound;

  for (;;)
  {
    Uint32 r = rngNext(rng);

    if (r >= threshold)
    {
      return (int)(r % bound);
    }
  }
}
