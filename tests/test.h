#pragma once

// The whole of the test framework, and it is deliberately this small.
//
// A game that generates every pixel and every sound effect at runtime does not
// have much surface that can be checked without a window and a sound card, so
// what is here tests the part that can be: the arithmetic, the level data as
// shipped, and the tables that map one to the other. That is a few hundred
// assertions, which is not worth a dependency - and a dependency is a second
// thing to install before `make test` works, on four platforms, for a project
// whose entire build is a makefile and pkg-config.
//
// Every macro reports the file, the line and the values, and then carries on
// rather than stopping: one run should say everything that is wrong with the
// tree, not the first thing.

#include <stdio.h>
#include <string.h>
#include <math.h>

extern int testsRun;
extern int testsFailed;
extern const char *currentTest;

#define CHECK(cond)                                                    \
  do                                                                   \
  {                                                                    \
    testsRun++;                                                        \
    if (!(cond))                                                       \
    {                                                                  \
      testsFailed++;                                                   \
      printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);         \
    }                                                                  \
  } while (0)

#define CHECK_INT(actual, expected)                                    \
  do                                                                   \
  {                                                                    \
    testsRun++;                                                        \
    long long a_ = (long long)(actual);                                \
    long long e_ = (long long)(expected);                              \
    if (a_ != e_)                                                      \
    {                                                                  \
      testsFailed++;                                                   \
      printf("  FAIL %s:%d  %s\n       got %lld, wanted %lld\n",       \
             __FILE__, __LINE__, #actual, a_, e_);                     \
    }                                                                  \
  } while (0)

// Floats are compared with a tolerance because most of what is checked here
// came out of a divide or a square root.
#define CHECK_NEAR(actual, expected, tol)                              \
  do                                                                   \
  {                                                                    \
    testsRun++;                                                        \
    double a_ = (double)(actual);                                      \
    double e_ = (double)(expected);                                    \
    if (!(fabs(a_ - e_) <= (double)(tol)))                             \
    {                                                                  \
      testsFailed++;                                                   \
      printf("  FAIL %s:%d  %s\n       got %g, wanted %g (+/- %g)\n",  \
             __FILE__, __LINE__, #actual, a_, e_, (double)(tol));      \
    }                                                                  \
  } while (0)

#define CHECK_STR(actual, expected)                                    \
  do                                                                   \
  {                                                                    \
    testsRun++;                                                        \
    const char *a_ = (actual);                                         \
    const char *e_ = (expected);                                       \
    if (a_ == NULL || e_ == NULL || strcmp(a_, e_) != 0)               \
    {                                                                  \
      testsFailed++;                                                   \
      printf("  FAIL %s:%d  %s\n       got \"%s\", wanted \"%s\"\n",   \
             __FILE__, __LINE__, #actual,                              \
             a_ != NULL ? a_ : "(null)", e_ != NULL ? e_ : "(null)");  \
    }                                                                  \
  } while (0)

// Names the group a failure belongs to, and prints it whether or not anything
// in it fails - a run that says nothing about a group is a group that did not
// run, and that should be visible.
#define TEST_GROUP(name)                                               \
  do                                                                   \
  {                                                                    \
    currentTest = (name);                                              \
    printf("- %s\n", (name));                                          \
  } while (0)

// Each of these is one file of tests; testsMain() below calls them in order.
void testGlobals(void);
void testLevels(void);
void testProgress(void);
void testSaveParser(void);
void testLevelParser(void);
