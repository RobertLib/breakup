// The high score table and the combo ladder.
//
// **Everything here runs with setSaveScripted(true).** That is not a detail:
// save.c resolves its path with SDL_GetPrefPath(), which is the player's own
// settings directory on the machine running the tests, and recordScore()
// writes the file as soon as it changes anything. Scripted
// mode is the switch that makes loadSave() leave the shipped defaults in place
// and writeSave() return without opening anything, and it is what keeps a test
// run from overwriting somebody's actual progress. testsMain() sets it before
// any test runs and it is set again here, because the cost of getting this
// wrong is somebody's high scores.

#include "test.h"

#include "../src/globals.h"
#include "../src/level-manager.h"
#include "../src/lib/save.h"
#include "../src/balls/balls.h"

static void resetSave(void)
{
  setSaveScripted(true);
  loadSave();
}

static void testDefaults(void)
{
  TEST_GROUP("save: the defaults a fresh install starts from");

  resetSave();

  CHECK_NEAR(saveData.musicVol, 0.7f, 1e-6);
  CHECK_NEAR(saveData.sfxVol, 0.8f, 1e-6);
  CHECK(!saveData.fullscreen);

  for (int i = 0; i < HIGH_SCORE_COUNT; i++)
  {
    CHECK_INT(saveData.highScores[i], 0);
    CHECK_INT(saveData.highActs[i], 0);
  }
}

static void testHighScores(void)
{
  TEST_GROUP("recordScore: the table stays sorted and the right length");

  resetSave();

  // A run worth nothing is not a run. runFinish() calls this on every exit,
  // including one where the player never hit a brick.
  CHECK_INT(recordScore(0, 1), -1);
  CHECK_INT(recordScore(-100, 1), -1);
  CHECK_INT(saveData.highScores[0], 0);

  // The first score takes the top of an empty table.
  CHECK_INT(recordScore(1000, 3), 0);
  CHECK_INT(saveData.highScores[0], 1000);
  CHECK_INT(saveData.highActs[0], 3);

  // A better one pushes it down rather than replacing it.
  CHECK_INT(recordScore(5000, 7), 0);
  CHECK_INT(saveData.highScores[0], 5000);
  CHECK_INT(saveData.highActs[0], 7);
  CHECK_INT(saveData.highScores[1], 1000);
  CHECK_INT(saveData.highActs[1], 3);

  // A worse one goes underneath, and the rank it comes back with is what the
  // run-over screen turns into "NEW HIGH SCORE - #2".
  CHECK_INT(recordScore(2000, 5), 1);
  CHECK_INT(saveData.highScores[0], 5000);
  CHECK_INT(saveData.highScores[1], 2000);
  CHECK_INT(saveData.highScores[2], 1000);

  // Fill the table and then push at it from below.
  resetSave();

  for (int i = 0; i < HIGH_SCORE_COUNT; i++)
  {
    recordScore((HIGH_SCORE_COUNT - i) * 1000, i + 1);
  }

  for (int i = 0; i < HIGH_SCORE_COUNT; i++)
  {
    CHECK_INT(saveData.highScores[i], (HIGH_SCORE_COUNT - i) * 1000);
  }

  // Worse than everything on a full table does not make the list.
  CHECK_INT(recordScore(1, 1), -1);
  CHECK_INT(saveData.highScores[HIGH_SCORE_COUNT - 1], 1000);

  // Better than the last one takes its place and drops it off the bottom -
  // the table must not grow.
  CHECK_INT(recordScore(1500, 9), HIGH_SCORE_COUNT - 1);
  CHECK_INT(saveData.highScores[HIGH_SCORE_COUNT - 1], 1500);
  CHECK_INT(saveData.highActs[HIGH_SCORE_COUNT - 1], 9);

  // Descending, always: the menu prints it in order and colours the first row.
  for (int i = 1; i < HIGH_SCORE_COUNT; i++)
  {
    CHECK(saveData.highScores[i - 1] >= saveData.highScores[i]);
  }

  // A new best takes rank 0, which is the one value the screens test for.
  CHECK_INT(recordScore(999999, 4), 0);
  CHECK_INT(saveData.highScores[0], 999999);
}

static void testOutOfRangeInputs(void)
{
  TEST_GROUP("save: acts that cannot be do not break the table");

  // recordScore() is handed whatever act the run reached. A nonsense act must
  // not stop the score being recorded, must not be stored as it came, and must
  // not leave the table unsorted or the wrong length.
  resetSave();

  CHECK_INT(recordScore(300, -1), 0);
  CHECK_INT(recordScore(200, 2000000000), 1);
  CHECK_INT(recordScore(100, 0), 2);
  CHECK_INT(recordScore(400, -1000000), 0);

  CHECK_INT(saveData.highActs[0], 0);
  CHECK_INT(saveData.highActs[1], 0);
  CHECK_INT(saveData.highActs[2], 9999);

  CHECK_INT(saveData.highScores[0], 400);
  CHECK_INT(saveData.highScores[1], 300);
  CHECK_INT(saveData.highScores[2], 200);
  CHECK_INT(saveData.highScores[3], 100);
  CHECK_INT(saveData.highScores[4], 0);

  for (int i = 1; i < HIGH_SCORE_COUNT; i++)
  {
    CHECK(saveData.highScores[i - 1] >= saveData.highScores[i]);
  }

  for (int i = 0; i < HIGH_SCORE_COUNT; i++)
  {
    CHECK(saveData.highScores[i] >= 0);
  }
}

static void testCombo(void)
{
  TEST_GROUP("combo: one step per five links, capped at five");

  resetCombo();
  CHECK_INT(comboMultiplier(), 1);

  // The multiplier steps up every fifth unbroken break. The links are added
  // straight to `combo` rather than through addComboLinks(), which announces a
  // step with a floating label - and that needs a font this runner never loads.
  for (int i = 0; i < 4; i++)
  {
    combo++;
    CHECK_INT(comboMultiplier(), 1);
  }

  combo++; // fifth
  CHECK_INT(comboMultiplier(), 2);

  for (int i = 0; i < 5; i++)
  {
    combo++;
  }
  CHECK_INT(comboMultiplier(), 3);

  // And stops climbing at the campaign's cap. A run can lift it (see
  // tests/test_run.c); nothing outside one can.
  for (int i = 0; i < 500; i++)
  {
    combo++;
  }
  CHECK_INT(comboMultiplier(), 5);

  // Touching the paddle ends the chain.
  resetCombo();
  CHECK_INT(comboMultiplier(), 1);
  CHECK_INT(combo, 0);
}

void testProgress(void)
{
  setSaveScripted(true);

  initializeLevelManager();

  testDefaults();
  testHighScores();
  testOutOfRangeInputs();
  testCombo();

  destroyLevelManager();

  // Left as the defaults rather than as whatever the last test wrote, so that
  // nothing downstream reads a table full of test scores.
  resetSave();
}
