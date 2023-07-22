// The save file parser, fed text directly.
//
// save.txt is plain text in the player's own settings directory, and nothing
// stops it being edited by hand, truncated by a crash, or written by an older
// build. loadSaveFromText() is loadSave() with the file I/O taken off the
// front, which is what lets each of those be handed to it here without a file
// on disk and without touching whoever's real save is on this machine.
//
// Everything here runs with setSaveScripted(true) - see the note at the top of
// tests/test_progress.c for why that is not optional.

#include "test.h"

#include "../src/globals.h"
#include "../src/level-manager.h"
#include "../src/lib/save.h"

#include <stdlib.h>

static void load(const char *text)
{
  loadSaveFromText(text, strlen(text));
}

// Puts something recognisably not-default in every field, so that a test can
// tell "reset to defaults" from "left alone".
static void scribble(void)
{
  saveData.unlocked = 9;
  saveData.musicVol = 0.11f;
  saveData.sfxVol = 0.22f;
  saveData.fullscreen = true;

  for (int i = 0; i < HIGH_SCORE_COUNT; i++)
  {
    saveData.highScores[i] = 777 + i;
    saveData.highLevels[i] = 3;
  }
}

static void checkDefaults(void)
{
  CHECK_INT(saveData.unlocked, 1);
  CHECK_NEAR(saveData.musicVol, 0.7f, 1e-6);
  CHECK_NEAR(saveData.sfxVol, 0.8f, 1e-6);
  CHECK(!saveData.fullscreen);

  for (int i = 0; i < HIGH_SCORE_COUNT; i++)
  {
    CHECK_INT(saveData.highScores[i], 0);
    CHECK_INT(saveData.highLevels[i], 0);
  }
}

static void testRoundTrip(void)
{
  TEST_GROUP("save parser: what writeSave() writes comes back whole");

  const int total = getNumberOfLevels();
  const int unlocked = total >= 5 ? 5 : total;

  // Built in exactly the shape writeSave() produces: the four settings, then
  // one hsN=score,level line per slot, each line newline-terminated.
  char text[512];
  int len = snprintf(text, sizeof(text),
                     "unlocked=%d\nmusic=%.2f\nsfx=%.2f\nfullscreen=%d\n",
                     unlocked, 0.35f, 0.60f, 1);

  for (int i = 0; i < HIGH_SCORE_COUNT; i++)
  {
    len += snprintf(text + len, sizeof(text) - (size_t)len, "hs%d=%d,%d\n",
                    i, (HIGH_SCORE_COUNT - i) * 1000, i + 1);
  }

  scribble();
  load(text);

  CHECK_INT(saveData.unlocked, unlocked);
  CHECK_NEAR(saveData.musicVol, 0.35f, 1e-6);
  CHECK_NEAR(saveData.sfxVol, 0.60f, 1e-6);
  CHECK(saveData.fullscreen);

  for (int i = 0; i < HIGH_SCORE_COUNT; i++)
  {
    CHECK_INT(saveData.highScores[i], (HIGH_SCORE_COUNT - i) * 1000);
    CHECK_INT(saveData.highLevels[i], i + 1);
  }

  // fullscreen is a flag: anything non-zero is on, and 0 is off.
  load("fullscreen=0\n");
  CHECK(!saveData.fullscreen);
  load("fullscreen=2\n");
  CHECK(saveData.fullscreen);
}

static void testEmptyAndGarbage(void)
{
  TEST_GROUP("save parser: nothing, and nonsense, both mean the defaults");

  // An empty buffer is a fresh install - and it must *reset*, not merely
  // leave whatever was in saveData before.
  scribble();
  loadSaveFromText("", 0);
  checkDefaults();

  // Lines that are not key=value, keys nobody knows, a value with no key, and
  // a high score slot that does not exist.
  scribble();
  load("hello there\n"
       "=\n"
       "=42\n"
       "unlocked\n"
       "colour=blue\n"
       "hs5=100,1\n"
       "hs-1=100,1\n"
       "\n"
       "\n");
  checkDefaults();

  // The buffer is bounded by `size`, not by a terminator: what lies past it
  // must not be read. Only the first line is inside the size handed over.
  const char *twoLines = "unlocked=3\nmusic=0.5\n";
  scribble();
  loadSaveFromText(twoLines, strlen("unlocked=3\n"));
  CHECK_INT(saveData.unlocked, getNumberOfLevels() >= 3 ? 3 : getNumberOfLevels());
  CHECK_NEAR(saveData.musicVol, 0.7f, 1e-6);
}

static void testVolumes(void)
{
  TEST_GROUP("save parser: volumes are clamped, and a NaN does not get in");

  // sscanf("%f") accepts "nan" and "inf". The options screen does
  // (int)roundf(vol * 10) on these, which is undefined behaviour for a NaN -
  // so the clamp on the way in is the only thing between an edited save file
  // and that. See clampFloat() in globals.h.
  load("music=nan\nsfx=inf\n");
  CHECK(!isnan(saveData.musicVol));
  CHECK(!isnan(saveData.sfxVol));
  CHECK(saveData.musicVol >= 0.0f && saveData.musicVol <= 1.0f);
  CHECK(saveData.sfxVol >= 0.0f && saveData.sfxVol <= 1.0f);
  CHECK_NEAR(saveData.sfxVol, 1.0f, 1e-6);

  load("music=-nan\nsfx=-inf\n");
  CHECK(!isnan(saveData.musicVol));
  CHECK(saveData.musicVol >= 0.0f && saveData.musicVol <= 1.0f);
  CHECK_NEAR(saveData.sfxVol, 0.0f, 1e-6);

  load("music=-5\nsfx=7\n");
  CHECK_NEAR(saveData.musicVol, 0.0f, 1e-6);
  CHECK_NEAR(saveData.sfxVol, 1.0f, 1e-6);

  // An unparseable value leaves the default, and the other line still parses.
  load("music=loud\nsfx=0.25\n");
  CHECK_NEAR(saveData.musicVol, 0.7f, 1e-6);
  CHECK_NEAR(saveData.sfxVol, 0.25f, 1e-6);
}

static void testUnlocked(void)
{
  TEST_GROUP("save parser: unlocked stays inside [1, level count]");

  const int total = getNumberOfLevels();
  const char *cases[] = {
      "unlocked=0\n",
      "unlocked=-3\n",
      "unlocked=-2147483648\n",
      "unlocked=2147483647\n",
      "unlocked=99999999999999999999\n",
      "unlocked=abc\n",
      "unlocked=\n",
      "unlocked=1000\n",
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
  {
    scribble();
    load(cases[i]);
    CHECK(saveData.unlocked >= 1 && saveData.unlocked <= total);
  }

  // The two ends exactly: zero and a negative both mean "nothing unlocked",
  // which is one level, and too many means all of them.
  load("unlocked=0\n");
  CHECK_INT(saveData.unlocked, 1);
  load("unlocked=-3\n");
  CHECK_INT(saveData.unlocked, 1);
  load("unlocked=1000\n");
  CHECK_INT(saveData.unlocked, total);

  // Not a number at all is not a value, so the default stands.
  load("unlocked=abc\n");
  CHECK_INT(saveData.unlocked, 1);
}

static void testHighScoreLines(void)
{
  TEST_GROUP("save parser: a score is never negative, a level always exists");

  const int total = getNumberOfLevels();

  scribble();
  load("hs0=99999999999999999999,1\n"
       "hs1=-5,2\n"
       "hs2=100,999\n"
       "hs3=50,-4\n"
       "hs4=abc,1\n");

  for (int i = 0; i < HIGH_SCORE_COUNT; i++)
  {
    CHECK(saveData.highScores[i] >= 0);
    CHECK(saveData.highLevels[i] >= 0 && saveData.highLevels[i] <= total);
  }

  // The table must never print "-00005" at anybody.
  CHECK_INT(saveData.highScores[1], 0);
  CHECK_INT(saveData.highLevels[1], 2);

  // A level past the end of what this build has is clamped to the last one.
  CHECK_INT(saveData.highScores[2], 100);
  CHECK_INT(saveData.highLevels[2], total);

  CHECK_INT(saveData.highScores[3], 50);
  CHECK_INT(saveData.highLevels[3], 0);

  // hs4 did not parse, so it is the default rather than what was there.
  CHECK_INT(saveData.highScores[4], 0);
  CHECK_INT(saveData.highLevels[4], 0);

  // One value where two are wanted is not a score line.
  load("hs0=500\n");
  CHECK_INT(saveData.highScores[0], 0);
}

static void testLineEndings(void)
{
  TEST_GROUP("save parser: CRLF, no trailing newline, odd spacing");

  const int total = getNumberOfLevels();

  // A file that went through Notepad, or through git on Windows.
  scribble();
  load("unlocked=3\r\nmusic=0.5\r\nsfx=0.25\r\nfullscreen=1\r\nhs0=42,2\r\n");
  CHECK_INT(saveData.unlocked, total >= 3 ? 3 : total);
  CHECK_NEAR(saveData.musicVol, 0.5f, 1e-6);
  CHECK_NEAR(saveData.sfxVol, 0.25f, 1e-6);
  CHECK(saveData.fullscreen);
  CHECK_INT(saveData.highScores[0], 42);
  CHECK_INT(saveData.highLevels[0], 2);

  // The last line without its newline still counts.
  scribble();
  load("unlocked=2\nmusic=0.1");
  CHECK_INT(saveData.unlocked, total >= 2 ? 2 : total);
  CHECK_NEAR(saveData.musicVol, 0.1f, 1e-6);

  // A key with trailing spaces before the '=' is not the key that is written,
  // so whatever the parser makes of it, the value has to stay valid and the
  // well-formed lines around it have to keep parsing.
  scribble();
  load("music   =0.5\nfullscreen =1\nunlocked=3\nsfx=0.3\n");
  CHECK(saveData.musicVol >= 0.0f && saveData.musicVol <= 1.0f);
  CHECK(!isnan(saveData.musicVol));
  CHECK_INT(saveData.unlocked, total >= 3 ? 3 : total);
  CHECK_NEAR(saveData.sfxVol, 0.3f, 1e-6);

  // A single line that is nothing but a newline, and a file that is nothing
  // but newlines.
  load("\n");
  checkDefaults();
  load("\r\n\r\n\r\n");
  checkDefaults();
}

static void testLargeBuffer(void)
{
  TEST_GROUP("save parser: a buffer far bigger than any real save");

  // loadSave() refuses a file over 8192 bytes; whether or not the text entry
  // point does the same, twenty kilobytes of valid lines must parse or be
  // refused cleanly - not run off the end of a fixed buffer.
  const size_t size = 20 * 1024;
  char *big = malloc(size + 1);

  CHECK(big != NULL);

  if (big == NULL)
  {
    return;
  }

  size_t len = 0;
  const char *line = "unlocked=2\nmusic=0.5\n";
  const size_t lineLen = strlen(line);

  while (len + lineLen <= size)
  {
    memcpy(big + len, line, lineLen);
    len += lineLen;
  }
  big[len] = '\0';

  const int total = getNumberOfLevels();

  scribble();
  loadSaveFromText(big, len);
  CHECK(saveData.unlocked >= 1 && saveData.unlocked <= total);
  CHECK(saveData.musicVol >= 0.0f && saveData.musicVol <= 1.0f);

  // And one enormous line with no newline in it at all.
  memset(big, 'x', size);
  big[size] = '\0';
  scribble();
  loadSaveFromText(big, size);
  CHECK(saveData.unlocked >= 1 && saveData.unlocked <= total);

  free(big);
}

void testSaveParser(void)
{
  setSaveScripted(true);

  // unlocked and the high score levels are clamped against the level count,
  // so the levels have to be loaded for the clamps to mean anything.
  initializeLevelManager();

  testRoundTrip();
  testEmptyAndGarbage();
  testVolumes();
  testUnlocked();
  testHighScoreLines();
  testLineEndings();
  testLargeBuffer();

  destroyLevelManager();

  // Back to the defaults, so nothing downstream reads what these tests wrote.
  loadSaveFromText("", 0);
}
