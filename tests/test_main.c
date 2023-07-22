// The test runner.
//
// It links every object the game does except src/main.o and brings its own
// main(), which is why the tests can call into the real level manager, the
// real save code and the real audio tables rather than a copy of them. Nothing
// here initializes SDL: no window, no renderer, no mixer, no sound card - so
// what can be tested is what does not need one, and nothing in here should
// start needing one.
//
// Exit status is what `make test` and CI read: 0 when every check passed.

#include "test.h"

#include "../src/lib/save.h"

int testsRun;
int testsFailed;
const char *currentTest = "";

int main(void)
{
  // Before anything else, and before any test has a chance to call
  // recordScore() or unlockLevel(). Both of those write the save file the
  // moment they change something, and the path they write to is the real one
  // for whoever is running this. See the header of tests/test_progress.c.
  setSaveScripted(true);

  printf("breakup tests\n\n");

  testGlobals();
  testLevels();
  testProgress();

  printf("\n%d checks, %d failed\n", testsRun, testsFailed);

  if (testsFailed == 0)
  {
    printf("ok\n");
    return 0;
  }

  return 1;
}
