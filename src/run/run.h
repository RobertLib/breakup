#pragma once

#include "../globals.h"
#include "../level-types.h"
#include "perks.h"

// The game is a run: four acts, each three levels and a boss drawn from the
// pool of the matching world - in a different order every time, and from the
// second act on often under a mutator that reshapes the level. After every
// level the player takes one of three perks for the rest of the run. After
// every boss they choose: cash out and keep the score, or go deeper - the
// score multiplier goes up, a curse is added that stays for the rest of the
// run, and half of whatever they score from there on is lost if they die
// before the next chance to cash out.
//
// The fourth act ends with The Sovereign, and beating it wins the run. The
// choice after it is to finish there, or to go on into an endless run of acts
// drawn from the whole game, for as long as the player lasts.
//
// A daily run is the same thing with the seed taken from the date, so it is
// one run per day and the same one for everybody. Everything that shapes it -
// the levels, the mutators, the perks on offer, the curses - comes out of the
// game's own RNG (run/rng.h), which is what makes that true across platforms.
// It also offers every perk whether or not this save has unlocked it, so that
// two players of one daily are playing the same game.
//
// Outside a run - the menus, the test runner - every tuning function below
// answers the neutral value.

#define RUN_STAGES_PER_ACT 4 // three levels, then a boss
#define RUN_ACTS 4           // and the last boss of the last one wins the run
#define RUN_MAX_OFFERS 3

// What beating The Sovereign is worth, before the multiplier.
#define RUN_VICTORY_BONUS 10000

typedef enum Mutator
{
  MUTATOR_NONE,
  MUTATOR_MIRRORED,  // the level flipped left to right
  MUTATOR_ARMORED,   // every brick one grade tougher
  MUTATOR_BRITTLE,   // every brick one grade weaker
  MUTATOR_GOLD_RUSH, // one basic brick in five is gold
  MUTATOR_SWARM,     // enemies respawn three times as fast
  MUTATOR_SURGE,     // capsules drop twice as often
  MUTATOR_COUNT
} Mutator;

typedef enum Curse
{
  CURSE_OVERDRIVE, // balls are faster
  CURSE_HARDENED,  // basic bricks take two hits
  CURSE_DROUGHT,   // fewer capsules
  CURSE_NARROW,    // a shorter paddle
  CURSE_HUNTED,    // enemies respawn faster
  CURSE_COUNT
} Curse;

typedef enum RunEnd
{
  RUN_END_DIED,
  RUN_END_QUIT, // abandoned from the pause menu or by closing the window
  RUN_END_CASHED,
  RUN_END_WON // finished after The Sovereign
} RunEnd;

typedef struct RunResult
{
  RunEnd end;
  bool won; // The Sovereign fell, whether or not the run stopped there
  bool daily;
  int dailyDate; // YYYYMMDD
  int score;     // what the run is recorded at, after any loss
  int lost;      // what dying or abandoning cost
  int act;       // 1-based
  int stageInAct;
  int perks;
  int rank; // place in the high score table, -1 for none
  bool newDailyBest;
} RunResult;

const char *mutatorName(Mutator mutator);
const char *mutatorDescription(Mutator mutator);
const char *curseName(Curse curse);
const char *curseDescription(Curse curse);

// Today's local date as YYYYMMDD, or 0 if the clock cannot be read.
int todayStamp(void);

// Begins a run and returns the level index of its first stage. A daily run is
// seeded from todayStamp(), an ordinary one from rand() (which BREAKUP_SEED
// pins, so a capture of a run is reproducible).
int startRun(bool daily);
int startRunWithSeed(Uint32 seed, bool daily);

bool runActive(void);
bool runIsDaily(void);
Uint32 runSeed(void);

int runStage(void);      // 0-based, counting every level of the run
int runAct(void);        // 0-based
int runStageInAct(void); // 0-based; the last one is the boss
bool runStageIsBoss(void);
int runLevel(void); // the level index being played

// The last boss of the last act: the stage whose end wins the run.
bool runAtFinale(void);

// Records that the finale fell and returns the victory bonus, scaled by the
// multiplier - once; 0 for every call after the first.
int runClaimVictory(void);
bool runWon(void);

// Development helper: plays `level` for the current stage instead of the one
// the seed drew, and without a mutator. BREAKUP_LEVEL uses it.
void runForceLevel(int level);

// Moves to the next stage and returns its level index.
int runAdvance(void);

Mutator runMutator(void);

bool runHasPerk(Perk perk);
int runPerkCount(void);

// Up to RUN_MAX_OFFERS perks for the stage just cleared, written to `out`;
// returns how many. The offer for a stage depends on the seed, the stage and
// what is already held, and on nothing else.
int runOfferPerks(Perk out[RUN_MAX_OFFERS], int lives);

// Holds `perk` for the rest of the run. An instant perk is not held - its
// effect is the caller's to apply.
void runTakePerk(Perk perk);

bool runHasCurse(Curse curse);
int runCurseCount(void);

// The curse going deeper after this act would add, or CURSE_COUNT when every
// curse is already on.
Curse runNextCurse(void);

// Going deeper: the curse above goes on, the multiplier goes up, and `score`
// becomes the checkpoint that dying measures its loss from.
void runGoDeeper(int score);

float runScoreMultiplier(void);
float runNextScoreMultiplier(void);

// Points as a run scores them: `points` times the multiplier. The identity
// outside a run.
int runScoreValue(int points);

// What dying right now would cost, for the cash-out screen and the HUD.
int runPointsAtRisk(int score);

// Tuning. Every one of these is 1 (or the unscaled value) outside a run.
float runBallSpeedScale(void);
int runComboCap(void);
float runItemDropScale(void);
float runPowerUpDurationScale(void);
float runPaddleWidthScale(void);
float runEnemyRespawnScale(void);

// Where the stage sits on the difficulty curve the levels were designed along,
// which is what the ball's launch speed is read from: a run's third level is
// not the game's twenty-sixth just because that is the file it drew. `level`
// outside a run.
int runSpeedLevel(int level);

// Called as each level begins; resets what a perk allows once per level.
void runLevelStarted(void);

// SAFETY NET: true, once per level, when the last ball should be saved.
bool runUseSafetyNet(void);

// The level as this run plays it: `getLevel(index)` with the stage's mutator
// and the run's curses applied to its pattern. Just getLevel(index) outside a
// run, or for any level but the one being played.
const Level *levelAsPlayed(int index);

// Ends the run at `score` and records it in the save. The result is kept for
// the run-over screen, which reads it back through runLastResult().
const RunResult *runFinish(RunEnd end, int score);
const RunResult *runLastResult(void);

// Development helper: a result to show without a run having ended, for
// BREAKUP_STATE=gameover and =win. Records nothing.
void runPreviewResult(RunEnd end, int score);
