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
#include "../src/lib/save.h"
#include "../src/run/perks.h"

#include <stdlib.h>

static void load(const char *text)
{
  loadSaveFromText(text, strlen(text));
}

// Puts something recognisably not-default in every field, so that a test can
// tell "reset to defaults" from "left alone".
static void scribble(void)
{
  saveData.musicVol = 0.11f;
  saveData.sfxVol = 0.22f;
  saveData.fullscreen = true;

  for (int i = 0; i < HIGH_SCORE_COUNT; i++)
  {
    saveData.highScores[i] = 777 + i;
    saveData.highActs[i] = 3;
  }

  saveData.perksUnlocked = unlockablePerks();
  saveData.dailyDate = 20260101;
  saveData.dailyBest = 55;

  saveData.story = false;
  saveData.runsStarted = 41;
  saveData.storySeen = STORY_SEEN_PROLOGUE | STORY_SEEN_FINALE;
}

static void checkDefaults(void)
{
  CHECK_NEAR(saveData.musicVol, 0.7f, 1e-6);
  CHECK_NEAR(saveData.sfxVol, 0.8f, 1e-6);
  CHECK(!saveData.fullscreen);

  for (int i = 0; i < HIGH_SCORE_COUNT; i++)
  {
    CHECK_INT(saveData.highScores[i], 0);
    CHECK_INT(saveData.highActs[i], 0);
  }

  CHECK_INT(saveData.perksUnlocked, starterPerks());
  CHECK_INT(saveData.dailyDate, 0);
  CHECK_INT(saveData.dailyBest, 0);

  // A fresh install has the story on and has seen none of it.
  CHECK(saveData.story);
  CHECK_INT(saveData.runsStarted, 0);
  CHECK_INT(saveData.storySeen, 0);
}

static void testRoundTrip(void)
{
  TEST_GROUP("save parser: what writeSave() writes comes back whole");

  Uint32 perks = starterPerks() | PERK_BIT(PERK_HUNTER) | PERK_BIT(PERK_MELTDOWN);

  // Built in exactly the shape writeSave() produces: the three settings, one
  // bestN=score,act line per slot, the perks and the daily, each line
  // newline-terminated.
  char text[512];
  int len = snprintf(text, sizeof(text), "music=%.2f\nsfx=%.2f\nfullscreen=%d\n",
                     0.35f, 0.60f, 1);

  for (int i = 0; i < HIGH_SCORE_COUNT; i++)
  {
    len += snprintf(text + len, sizeof(text) - (size_t)len, "best%d=%d,%d\n",
                    i, (HIGH_SCORE_COUNT - i) * 1000, i + 1);
  }

  snprintf(text + len, sizeof(text) - (size_t)len, "perks=%u\ndaily=20260928,1200\n",
           (unsigned)perks);

  scribble();
  load(text);

  CHECK_NEAR(saveData.musicVol, 0.35f, 1e-6);
  CHECK_NEAR(saveData.sfxVol, 0.60f, 1e-6);
  CHECK(saveData.fullscreen);

  for (int i = 0; i < HIGH_SCORE_COUNT; i++)
  {
    CHECK_INT(saveData.highScores[i], (HIGH_SCORE_COUNT - i) * 1000);
    CHECK_INT(saveData.highActs[i], i + 1);
  }

  CHECK_INT(saveData.perksUnlocked, perks);
  CHECK_INT(saveData.dailyDate, 20260928);
  CHECK_INT(saveData.dailyBest, 1200);

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
  // score slots that do not exist.
  scribble();
  load("hello there\n"
       "=\n"
       "=42\n"
       "music\n"
       "colour=blue\n"
       "best5=100,1\n"
       "best-1=100,1\n"
       "bestx=100,1\n"
       "\n"
       "\n");
  checkDefaults();

  // The buffer is bounded by `size`, not by a terminator: what lies past it
  // must not be read. Only the first line is inside the size handed over.
  const char *twoLines = "sfx=0.3\nmusic=0.5\n";
  scribble();
  loadSaveFromText(twoLines, strlen("sfx=0.3\n"));
  CHECK_NEAR(saveData.sfxVol, 0.3f, 1e-6);
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

static void testOldSave(void)
{
  TEST_GROUP("save parser: a save from before runs keeps its settings only");

  // What 1.0 wrote. Its settings are the player's and carry over; its high
  // scores are levels of a game that is no longer this one, and read as runs
  // they would put "ACT 27" at the top of the table - so they do not.
  scribble();
  load("unlocked=27\nmusic=0.40\nsfx=0.90\nfullscreen=1\n"
       "hs0=68400,27\nhs1=2450,1\nhs2=0,0\nhs3=0,0\nhs4=0,0\n");

  CHECK_NEAR(saveData.musicVol, 0.4f, 1e-6);
  CHECK_NEAR(saveData.sfxVol, 0.9f, 1e-6);
  CHECK(saveData.fullscreen);

  for (int i = 0; i < HIGH_SCORE_COUNT; i++)
  {
    CHECK_INT(saveData.highScores[i], 0);
    CHECK_INT(saveData.highActs[i], 0);
  }

  CHECK_INT(saveData.perksUnlocked, starterPerks());
}

static void testHighScoreLines(void)
{
  TEST_GROUP("save parser: a score is never negative, an act never absurd");

  scribble();
  load("best0=99999999999999999999,1\n"
       "best1=-5,2\n"
       "best2=100,999999\n"
       "best3=50,-4\n"
       "best4=abc,1\n");

  for (int i = 0; i < HIGH_SCORE_COUNT; i++)
  {
    CHECK(saveData.highScores[i] >= 0);
    CHECK(saveData.highActs[i] >= 0 && saveData.highActs[i] <= 9999);
  }

  // The table must never print "-00005" at anybody.
  CHECK_INT(saveData.highScores[1], 0);
  CHECK_INT(saveData.highActs[1], 2);

  CHECK_INT(saveData.highScores[2], 100);
  CHECK_INT(saveData.highActs[2], 9999);

  CHECK_INT(saveData.highScores[3], 50);
  CHECK_INT(saveData.highActs[3], 0);

  // best4 did not parse, so it is the default rather than what was there.
  CHECK_INT(saveData.highScores[4], 0);
  CHECK_INT(saveData.highActs[4], 0);

  // One value where two are wanted is not a score line.
  load("best0=500\n");
  CHECK_INT(saveData.highScores[0], 0);
}

static void testLineEndings(void)
{
  TEST_GROUP("save parser: CRLF, no trailing newline, odd spacing");

  // A file that went through Notepad, or through git on Windows.
  scribble();
  load("music=0.5\r\nsfx=0.25\r\nfullscreen=1\r\nbest0=42,2\r\ndaily=20260928,7\r\n");
  CHECK_NEAR(saveData.musicVol, 0.5f, 1e-6);
  CHECK_NEAR(saveData.sfxVol, 0.25f, 1e-6);
  CHECK(saveData.fullscreen);
  CHECK_INT(saveData.highScores[0], 42);
  CHECK_INT(saveData.highActs[0], 2);
  CHECK_INT(saveData.dailyBest, 7);

  // The last line without its newline still counts.
  scribble();
  load("best0=9,1\nmusic=0.1");
  CHECK_INT(saveData.highScores[0], 9);
  CHECK_NEAR(saveData.musicVol, 0.1f, 1e-6);

  // A key with trailing spaces before the '=' is not the key that is written,
  // so whatever the parser makes of it, the value has to stay valid and the
  // well-formed lines around it have to keep parsing.
  scribble();
  load("music   =0.5\nfullscreen =1\nbest0=3,1\nsfx=0.3\n");
  CHECK(saveData.musicVol >= 0.0f && saveData.musicVol <= 1.0f);
  CHECK(!isnan(saveData.musicVol));
  CHECK_INT(saveData.highScores[0], 3);
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
  const char *line = "best0=2,1\nmusic=0.5\n";
  const size_t lineLen = strlen(line);

  while (len + lineLen <= size)
  {
    memcpy(big + len, line, lineLen);
    len += lineLen;
  }
  big[len] = '\0';

  scribble();
  loadSaveFromText(big, len);
  CHECK_INT(saveData.highScores[0], 2);
  CHECK(saveData.musicVol >= 0.0f && saveData.musicVol <= 1.0f);

  // And one enormous line with no newline in it at all.
  memset(big, 'x', size);
  big[size] = '\0';
  scribble();
  loadSaveFromText(big, size);
  checkDefaults();

  free(big);
}

static void testRunKeys(void)
{
  TEST_GROUP("save parser: the perks and the daily");

  // A save that clears the starter bits still has them, and one that sets
  // every bit gets the collection and nothing past it.
  load("perks=0\n");
  CHECK_INT(saveData.perksUnlocked, starterPerks());
  load("perks=-1\n");
  CHECK_INT(saveData.perksUnlocked, unlockablePerks());

  // Nonsense stays out: a date that is not one, a pair with half of it
  // missing, a negative best.
  load("daily=123,99\n");
  CHECK_INT(saveData.dailyDate, 0);
  CHECK_INT(saveData.dailyBest, 0);

  load("daily=20260928\n");
  CHECK_INT(saveData.dailyDate, 0);

  load("daily=20260928,-40\n");
  CHECK_INT(saveData.dailyDate, 20260928);
  CHECK_INT(saveData.dailyBest, 0);
}

static void testStoryKeys(void)
{
  TEST_GROUP("save parser: the story switch, the run count, what has been seen");

  scribble();
  load("story=0\nruns=12\nseen=1\n");
  CHECK(!saveData.story);
  CHECK_INT(saveData.runsStarted, 12);
  CHECK_INT(saveData.storySeen, STORY_SEEN_PROLOGUE);

  load("story=1\nseen=3\n");
  CHECK(saveData.story);
  CHECK_INT(saveData.storySeen, STORY_SEEN_PROLOGUE | STORY_SEEN_FINALE);

  // A count that went negative counts nothing, and a mask with every bit set
  // skips the scenes there are and invents none.
  load("runs=-9\nseen=-1\n");
  CHECK_INT(saveData.runsStarted, 0);
  CHECK_INT(saveData.storySeen, STORY_SEEN_PROLOGUE | STORY_SEEN_FINALE);

  // Nonsense leaves the defaults.
  load("story=yes\nruns=many\n");
  CHECK(saveData.story);
  CHECK_INT(saveData.runsStarted, 0);
}

void testSaveParser(void)
{
  setSaveScripted(true);

  testRoundTrip();
  testEmptyAndGarbage();
  testVolumes();
  testOldSave();
  testHighScoreLines();
  testLineEndings();
  testLargeBuffer();
  testRunKeys();
  testStoryKeys();

  // Back to the defaults, so nothing downstream reads what these tests wrote.
  loadSaveFromText("", 0);
}
