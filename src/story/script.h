#pragma once

#include "../globals.h"
#include "../run/run.h"

// What the Sovereign says, and when.
//
// The being at the end of the fourth act is not only a boss: it built every
// world the run passes through, the crystals in its walls are its thoughts, and
// it has an opinion about each one the player breaks. It speaks when a run
// begins, between every two stages, when its core finally goes, and once more
// over the result when a run ends - a line or two at a time, so that the story
// is something a run passes through rather than something it waits on.
//
// This file is the words and the choosing of them, and nothing that draws: the
// builders below read a StoryContext and nothing else, which is what lets the
// tests hand them every act, stage, boss, mutator and curse there is and check
// that whatever comes out fits on the screen.

// A page is written in font24, which is 24 pixels a glyph: thirty of them is a
// line 720 wide, and four lines is what fits between the face and the map.
#define STORY_LINE_CHARS 30
#define STORY_PAGE_LINES 4
#define STORY_PAGE_TEXT 160
#define STORY_MAX_PAGES 8

typedef enum StoryMood
{
  STORY_MOOD_CALM,
  STORY_MOOD_AMUSED,
  STORY_MOOD_STERN,
  STORY_MOOD_PAINED,
  STORY_MOOD_SOFT,
  STORY_MOOD_COUNT
} StoryMood;

enum
{
  STORY_PAGE_SHATTER = 1 << 0, // the face breaks apart as this page begins
  STORY_PAGE_SILENT = 1 << 1,  // written with no voice and no face behind it
};

typedef struct StoryPage
{
  StoryMood mood;
  int flags;
  char text[STORY_PAGE_TEXT];
} StoryPage;

typedef enum StoryKind
{
  STORY_KIND_PROLOGUE,  // the run is about to begin
  STORY_KIND_INTERLUDE, // between two stages
  STORY_KIND_FINALE,    // The Sovereign has fallen
  STORY_KIND_EPILOGUE   // over the result of a run that has ended
} StoryKind;

typedef struct StoryScript
{
  StoryKind kind;
  int pageCount;
  StoryPage pages[STORY_MAX_PAGES];
  bool showMap; // the run's progress and the next stage, under the writing
  float glitch; // how damaged the face already is, 0..1
} StoryScript;

// Everything a line is chosen from.
typedef struct StoryContext
{
  int act;         // 0-based act of the stage about to be played (or last played)
  int stageInAct;  // 0-based; RUN_STAGES_PER_ACT - 1 is the boss
  int boss;        // `#boss N` of that stage, 0 when it is not a fight
  int clearedBoss; // `#boss N` of the stage just cleared, 0 when it was not one
  Mutator mutator;
  Curse newCurse; // taken since the last interlude, CURSE_COUNT for none
  int lives;
  bool won;         // The Sovereign has fallen this run
  bool daily;
  int runsStarted;  // before this one
  bool prologueSeen;
  bool finaleSeen;
} StoryContext;

// How much of its face the run has cost the Sovereign by `act` (0-based): 0 in
// the first world, more with every one after, and a good deal once its core
// has gone and it has been put back together out of what was left.
float storyDamage(int act, bool won);

// Forgets which lines this run has already used, so that a run does not hear
// the same aside twice until it has heard all of them.
void storyScriptReset(void);

void storyBuildPrologue(StoryScript *out, const StoryContext *ctx);
void storyBuildInterlude(StoryScript *out, const StoryContext *ctx);
void storyBuildFinale(StoryScript *out, const StoryContext *ctx);

// What the Sovereign says over a result. False when it says nothing: a run
// that was abandoned, or one that was won - the finale has had its say.
bool storyBuildEpilogue(StoryScript *out, const StoryContext *ctx, RunEnd end,
                        int lost);

// Breaks `text` into lines of at most STORY_LINE_CHARS, at spaces where it can
// and at '\n' where it is told to. Returns how many lines the text needs, which
// can be more than the STORY_PAGE_LINES written to `lines` - the tests check
// that it never is.
int storyWrap(const char *text, char lines[STORY_PAGE_LINES][STORY_LINE_CHARS + 1]);

// Every fixed line in the script, for the tests.
int storyLineCount(void);
const char *storyLineText(int index);
