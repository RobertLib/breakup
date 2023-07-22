// The arithmetic in src/globals.h and src/globals.c.
//
// None of it needs SDL to have been initialized, and most of it is the kind of
// thing that is only ever wrong at an edge: a clamp handed a NaN, a normalize
// handed a zero vector, a wrap handed a negative. Those edges are what this
// file is.

#include "test.h"

#include "../src/globals.h"

static void testClamp(void)
{
  TEST_GROUP("clamp: ints, floats and the values that are not numbers");

  CHECK_INT(clampInt(5, 0, 10), 5);
  CHECK_INT(clampInt(-3, 0, 10), 0);
  CHECK_INT(clampInt(99, 0, 10), 10);
  CHECK_INT(clampInt(0, 0, 0), 0);

  CHECK_NEAR(clampFloat(0.5f, 0.0f, 1.0f), 0.5f, 1e-6);
  CHECK_NEAR(clampFloat(-1.0f, 0.0f, 1.0f), 0.0f, 1e-6);
  CHECK_NEAR(clampFloat(2.0f, 0.0f, 1.0f), 1.0f, 1e-6);

  // The reason clampFloat is written `x >= a ? ... : a` rather than the
  // obvious way round. Every comparison against a NaN is false, so the obvious
  // form returned the NaN itself - and the one route a NaN has into this game
  // is `sfx=nan` in a save file the player can edit, which reached
  // (int)roundf() in the options screen and was undefined behaviour there. The
  // clamp is the only thing standing in that path, so it is checked.
  const float nanF = nanf("");
  const double nanD = nan("");

  CHECK(!isnan(clampFloat(nanF, 0.0f, 1.0f)));
  CHECK_NEAR(clampFloat(nanF, 0.0f, 1.0f), 0.0f, 1e-6);
  CHECK(!isnan(clampDouble(nanD, 0.0, 1.0)));
  CHECK_NEAR(clampDouble(nanD, 0.0, 1.0), 0.0, 1e-9);

  // And the same for an infinity, which sscanf will also take from that file.
  CHECK_NEAR(clampFloat(INFINITY, 0.0f, 1.0f), 1.0f, 1e-6);
  CHECK_NEAR(clampFloat(-INFINITY, 0.0f, 1.0f), 0.0f, 1e-6);

  // The _Generic macro has to pick the float function for float arguments and
  // the int one for ints; picking wrong is a truncation nothing would report.
  CHECK_NEAR(clamp(0.25f, 0.0f, 1.0f), 0.25f, 1e-6);
  CHECK_INT(clamp(7, 0, 3), 3);
}

static void testWrapIndex(void)
{
  TEST_GROUP("wrapIndex: stepping round a list, forwards and backwards");

  // The regression test for the level select grid. The UP arrow steps back by
  // a whole row, a row is at least GRID_COLS_MIN wide whatever the level count
  // is, and plain `%` answers with a negative when the step is the larger of
  // the two. Whatever the count and whatever the step, the answer has to be a
  // valid index.
  for (int count = 1; count <= 40; count++)
  {
    for (int selection = 0; selection < count; selection++)
    {
      for (int step = 1; step <= 10; step++)
      {
        int forward = wrapIndex(selection + step, count);
        int back = wrapIndex(selection - step, count);

        CHECK(forward >= 0 && forward < count);
        CHECK(back >= 0 && back < count);
      }
    }
  }

  // The values the old `%` got wrong, written out: two, three and four levels
  // against a five-wide row.
  CHECK_INT(wrapIndex(0 - 5, 2), 1);
  CHECK_INT(wrapIndex(0 - 5, 3), 1);
  CHECK_INT(wrapIndex(0 - 5, 4), 3);

  // The ordinary cases still behave.
  CHECK_INT(wrapIndex(0, 27), 0);
  CHECK_INT(wrapIndex(26, 27), 26);
  CHECK_INT(wrapIndex(27, 27), 0);
  CHECK_INT(wrapIndex(-1, 27), 26);
  CHECK_INT(wrapIndex(-28, 27), 26);

  // A list of nothing is the empty screen the level select already guards for;
  // this must not divide by zero on its way there.
  CHECK_INT(wrapIndex(0, 0), 0);
  CHECK_INT(wrapIndex(5, 0), 0);
  CHECK_INT(wrapIndex(-5, -3), 0);
}

static void testCollision(void)
{
  TEST_GROUP("checkCollision: the one predicate the whole field is built on");

  // Plainly overlapping.
  CHECK(checkCollision(0, 0, 10, 10, 5, 5, 10, 10));
  CHECK(checkCollision(5, 5, 10, 10, 0, 0, 10, 10));

  // Plainly apart, on each axis in turn.
  CHECK(!checkCollision(0, 0, 10, 10, 20, 0, 10, 10));
  CHECK(!checkCollision(0, 0, 10, 10, 0, 20, 10, 10));

  // Edges that touch but do not overlap. The test is strict on purpose: a
  // brick's right edge is the next brick's left edge, and a ball sitting
  // exactly on the seam must not be reported as inside both.
  CHECK(!checkCollision(0, 0, 10, 10, 10, 0, 10, 10));
  CHECK(!checkCollision(0, 0, 10, 10, 0, 10, 10, 10));

  // One pixel of overlap is an overlap.
  CHECK(checkCollision(0, 0, 10, 10, 9, 0, 10, 10));
  CHECK(checkCollision(0, 0, 10, 10, 0, 9, 10, 10));

  // Contained entirely.
  CHECK(checkCollision(0, 0, 100, 100, 40, 40, 5, 5));
}

static void testVectors(void)
{
  TEST_GROUP("vec2Norm and the easing helpers");

  Vec2 v = vec2Norm((Vec2){3.0f, 4.0f}, 10.0f);
  CHECK_NEAR(sqrtf(v.x * v.x + v.y * v.y), 10.0f, 1e-4);
  CHECK_NEAR(v.x, 6.0f, 1e-4);
  CHECK_NEAR(v.y, 8.0f, 1e-4);

  // A zero vector has no direction to scale. It has to come back as it went
  // in rather than as a pair of NaNs - the ball's velocity goes through here
  // every frame, and a NaN position is a ball that is instantly off the field.
  Vec2 zero = vec2Norm((Vec2){0.0f, 0.0f}, 5.0f);
  CHECK(!isnan(zero.x) && !isnan(zero.y));
  CHECK_NEAR(zero.x, 0.0f, 1e-6);
  CHECK_NEAR(zero.y, 0.0f, 1e-6);

  // Negative magnitude is not asked for anywhere, but it should still be a
  // number.
  Vec2 neg = vec2Norm((Vec2){1.0f, 0.0f}, -2.0f);
  CHECK_NEAR(neg.x, -2.0f, 1e-5);

  CHECK_NEAR(lerp(0.0f, 10.0f, 0.0f), 0.0f, 1e-6);
  CHECK_NEAR(lerp(0.0f, 10.0f, 1.0f), 10.0f, 1e-6);
  CHECK_NEAR(lerp(0.0f, 10.0f, 0.5f), 5.0f, 1e-6);
  CHECK_NEAR(lerp(-4.0f, 4.0f, 0.25f), -2.0f, 1e-6);

  // The ends are exact; the overshoot in between is the whole point of it.
  CHECK_NEAR(easeOutBack(0.0f), 0.0f, 1e-5);
  CHECK_NEAR(easeOutBack(1.0f), 1.0f, 1e-5);
  CHECK(easeOutBack(0.8f) > 1.0f);
}

static void testRandom(void)
{
  TEST_GROUP("frand: the range everything else assumes");

  // frand() is the source of every power-up roll, every particle and every
  // enemy heading. [0, 1) is what its callers are written against - a 1.0 out
  // of here would index one past the end of a weight table.
  bool sawLow = false;
  bool sawHigh = false;

  for (int i = 0; i < 20000; i++)
  {
    float f = frand();

    CHECK(f >= 0.0f && f < 1.0f);

    if (f < 0.25f)
    {
      sawLow = true;
    }
    if (f > 0.75f)
    {
      sawHigh = true;
    }

    float r = frandRange(-3.0f, 7.0f);
    CHECK(r >= -3.0f && r < 7.0f);
  }

  // A generator stuck on one value would pass every check above.
  CHECK(sawLow);
  CHECK(sawHigh);

  // An empty range is a constant, not an error.
  CHECK_NEAR(frandRange(2.0f, 2.0f), 2.0f, 1e-6);
}

void testGlobals(void)
{
  testClamp();
  testWrapIndex();
  testCollision();
  testVectors();
  testRandom();
}
