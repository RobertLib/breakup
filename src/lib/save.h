#pragma once

#include "../globals.h"

#define HIGH_SCORE_COUNT 5

typedef struct SaveData
{
  float musicVol;
  float sfxVol;
  bool fullscreen;

  // The best runs, best first, and the act each one reached (1-based).
  int highScores[HIGH_SCORE_COUNT];
  int highActs[HIGH_SCORE_COUNT];

  Uint32 perksUnlocked; // bitmask indexed by Perk (see src/run/perks.h)
  int dailyDate;        // YYYYMMDD of the daily run dailyBest belongs to, 0 for none
  int dailyBest;

  // The Sovereign's scenes (see src/story/story.h): whether they play at all,
  // how many runs it has watched begin - it counts them out loud - and which
  // of its once-only scenes this player has already sat through.
  bool story;
  int runsStarted;
  Uint32 storySeen; // STORY_SEEN_* bits
} SaveData;

#define STORY_SEEN_PROLOGUE (1u << 0)
#define STORY_SEEN_FINALE (1u << 1)

extern SaveData saveData;

// Turns this process's save file off, in both directions: loadSave() leaves the
// shipped defaults in place without opening the file, and writeSave() returns
// without touching it.
//
// It exists for a screenshot capture (BREAKUP_SHOT, see src/main.c). Two things
// go wrong without it, and neither is cosmetic: the run inherits the fullscreen
// flag of whoever started it, which is what decides the size every captured
// frame comes out at, and it overwrites their settings, progress and high scores
// on the way out. A capture is a measurement, and a measurement may not depend
// on - or alter - the machine it was taken on.
void setSaveScripted(bool scripted);

// The gentler switch, for the other development helpers (BREAKUP_STATE,
// BREAKUP_LEVEL, BREAKUP_SCORE, BREAKUP_KEYS, BREAKUP_SEED, BREAKUP_RUN):
// loadSave() still reads the file, so a developer sees the game as their own
// machine has it, but writeSave() never touches it. What those variables set up
// - a level jumped to, a score handed out, a run begun with every perk in
// hand - is not progress the player made and must not become their save on
// the way out.
void setSaveReadOnly(bool readOnly);

// Resets saveData to the shipped defaults and then parses `text` as a save
// file: `size` bytes, not necessarily NUL-terminated. No file is opened and
// neither switch above is consulted, which is what makes it testable. loadSave()
// is this, fed the file.
void loadSaveFromText(const char *text, size_t size);

void loadSave(void);

void writeSave(void);

// Inserts a run into the high-score table if it qualifies.
// Returns the rank (0-based) or -1 if it did not make the list.
int recordScore(int score, int actReached);
