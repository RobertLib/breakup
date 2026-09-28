#include "run.h"
#include "rng.h"
#include "../level-manager.h"
#include "../lib/save.h"
#include "../types.h"
#include <string.h>

// The level manager's own MAX_LEVELS; a run cannot draw a level it could not
// have loaded.
#define RUN_MAX_LEVELS 100

// What each descent adds to the score multiplier.
#define DESCENT_MULTIPLIER 0.5f

// The streams each part of a run draws from (see rngSeed() in rng.h). The
// mutator, perk and curse streams are offset by the stage or the act, so that
// each of those is a fresh sequence of its own rather than a position in a
// shared one - and the order in which the player happens to look at things
// cannot change what they are offered.
#define STREAM_LEVELS 1
#define STREAM_MUTATORS 0x100000
#define STREAM_PERKS 0x200000
#define STREAM_CURSES 0x300000

typedef struct RunState
{
  bool active;
  bool daily;
  int dailyDate;
  Uint32 seed;

  // The level order is the one sequence drawn from in turn: one number per
  // stage whatever the player does, so it depends on the seed alone.
  Rng levelRng;
  bool used[RUN_MAX_LEVELS];

  int stage;
  int level;
  Mutator mutator;

  Uint32 perks;
  Uint32 curses;
  int descents;
  int checkpoint; // the score at the last descent; -1 before the first

  bool won;
  bool safetyNetUsed;
} RunState;

static RunState run;
static RunResult lastResult;

// The copy levelAsPlayed() hands out when a mutator or a curse reshapes the
// pattern. There is one level being played at a time, and both of its readers
// (the bricks and the enemies) take what they need from it straight away.
static Level shapedLevel;

static const char *mutatorNames[MUTATOR_COUNT] = {
    "", "MIRRORED", "ARMORED", "BRITTLE", "GOLD RUSH", "SWARM", "SURGE"};

static const char *mutatorDescriptions[MUTATOR_COUNT] = {
    "",
    "THE LEVEL IS FLIPPED LEFT TO RIGHT",
    "BRICKS ARE ONE GRADE TOUGHER",
    "BRICKS ARE ONE GRADE WEAKER",
    "ONE BASIC BRICK IN FIVE IS GOLD",
    "ENEMIES RESPAWN THREE TIMES AS FAST",
    "CAPSULES DROP TWICE AS OFTEN"};

static const char *curseNames[CURSE_COUNT] = {
    "OVERDRIVE", "HARDENED", "DROUGHT", "NARROW", "HUNTED"};

static const char *curseDescriptions[CURSE_COUNT] = {
    "BALLS ARE 12% FASTER",
    "BASIC BRICKS TAKE TWO HITS",
    "CAPSULES DROP 40% LESS OFTEN",
    "THE PADDLE IS 20% SHORTER",
    "ENEMIES RESPAWN TWICE AS FAST"};

_Static_assert(SDL_arraysize(mutatorNames) == MUTATOR_COUNT, "one name per mutator");
_Static_assert(SDL_arraysize(mutatorDescriptions) == MUTATOR_COUNT, "one description per mutator");
_Static_assert(SDL_arraysize(curseNames) == CURSE_COUNT, "one name per curse");
_Static_assert(SDL_arraysize(curseDescriptions) == CURSE_COUNT, "one description per curse");
_Static_assert(PERK_COUNT <= 32, "saveData.perksUnlocked is a 32-bit mask");

const char *mutatorName(Mutator mutator)
{
  return (unsigned)mutator < (unsigned)MUTATOR_COUNT ? mutatorNames[mutator] : "";
}

const char *mutatorDescription(Mutator mutator)
{
  return (unsigned)mutator < (unsigned)MUTATOR_COUNT ? mutatorDescriptions[mutator] : "";
}

const char *curseName(Curse curse)
{
  return (unsigned)curse < (unsigned)CURSE_COUNT ? curseNames[curse] : "";
}

const char *curseDescription(Curse curse)
{
  return (unsigned)curse < (unsigned)CURSE_COUNT ? curseDescriptions[curse] : "";
}

int todayStamp(void)
{
  SDL_Time now;
  SDL_DateTime date;

  if (!SDL_GetCurrentTime(&now) || !SDL_TimeToDateTime(now, &date, true))
  {
    return 0;
  }

  return date.year * 10000 + date.month * 100 + date.day;
}

// ---------------------------------------------------------------------------
// The shape of a run
// ---------------------------------------------------------------------------

static int levelCount(void)
{
  int total = getNumberOfLevels();

  return total < RUN_MAX_LEVELS ? total : RUN_MAX_LEVELS;
}

static bool stageIsBoss(int stage)
{
  return stage % RUN_STAGES_PER_ACT == RUN_STAGES_PER_ACT - 1;
}

// Which bosses an act's last stage may be, by `#boss N` number: the seven in
// pairs, in the order they get harder, The Sovereign alone at the end of the fourth act, and from
// the fifth on anything from the back half of the game.
static void bossTier(int act, int *lo, int *hi)
{
  static const int tiers[4][2] = {{1, 2}, {3, 4}, {5, 6}, {7, 7}};

  if (act < 4)
  {
    *lo = tiers[act][0];
    *hi = tiers[act][1];
  }
  else
  {
    *lo = 4;
    *hi = 7;
  }
}

// The levels a stage may draw from. Every narrowing falls back to something
// wider when it comes up empty, because the level files are a mod point: a
// tree with no bosses, or a world with no levels in it, still makes a run.
static int collectCandidates(int stage, int *out)
{
  int act = stage / RUN_STAGES_PER_ACT;
  int total = levelCount();
  int n = 0;

  if (stageIsBoss(stage))
  {
    int lo, hi;
    bossTier(act, &lo, &hi);

    for (int i = 0; i < total; i++)
    {
      int boss = getLevel(i)->boss;

      if (boss >= lo && boss <= hi)
      {
        out[n++] = i;
      }
    }

    for (int i = 0; n == 0 && i < total; i++)
    {
      if (isBossLevel(i))
      {
        out[n++] = i;
      }
    }

    if (n > 0)
    {
      return n;
    }
  }

  int world = act < WORLD_COUNT ? act : -1;

  for (int i = 0; i < total; i++)
  {
    if (!isBossLevel(i) && (world < 0 || worldForLevel(i) == world))
    {
      out[n++] = i;
    }
  }

  for (int i = 0; n == 0 && i < total; i++)
  {
    if (!isBossLevel(i))
    {
      out[n++] = i;
    }
  }

  for (int i = 0; n == 0 && i < total; i++)
  {
    out[n++] = i;
  }

  return n;
}

// Prefers a level this run has not played yet, and only repeats one once the
// candidates have all been used.
static int drawLevel(int stage)
{
  int candidates[RUN_MAX_LEVELS];
  int n = collectCandidates(stage, candidates);

  if (n == 0)
  {
    return 0;
  }

  int fresh[RUN_MAX_LEVELS];
  int m = 0;

  for (int i = 0; i < n; i++)
  {
    if (!run.used[candidates[i]])
    {
      fresh[m++] = candidates[i];
    }
  }

  const int *pool = m > 0 ? fresh : candidates;
  int count = m > 0 ? m : n;
  int level = pool[rngBelow(&run.levelRng, count)];

  run.used[level] = true;

  return level;
}

static bool levelHasSpawners(const Level *level)
{
  for (int i = 0; i < LEVEL_PATTERN_LENGTH; i++)
  {
    char c = level->pattern[i];

    if (c == 'E' || c == 'V' || c == 'W' || c == 'U')
    {
      return true;
    }
  }

  return false;
}

static Mutator drawMutator(int stage, int level)
{
  if (stageIsBoss(stage))
  {
    return MUTATOR_NONE;
  }

  // The first act is the levels as they were designed; after that a mutator
  // gets steadily more likely.
  static const int chance[4] = {0, 35, 55, 75};
  int act = stage / RUN_STAGES_PER_ACT;

  Rng rng;
  rngSeed(&rng, run.seed, STREAM_MUTATORS + (Uint64)stage);

  if (rngBelow(&rng, 100) >= chance[clamp(act, 0, 3)])
  {
    return MUTATOR_NONE;
  }

  Mutator eligible[MUTATOR_COUNT];
  int n = 0;

  for (int m = MUTATOR_NONE + 1; m < MUTATOR_COUNT; m++)
  {
    // A swarm of nothing is no mutator at all, and ARMORED on top of HARDENED
    // turns every basic brick in the level into a three-hitter.
    if (m == MUTATOR_SWARM && !levelHasSpawners(getLevel(level)))
    {
      continue;
    }

    if (m == MUTATOR_ARMORED && runHasCurse(CURSE_HARDENED))
    {
      continue;
    }

    eligible[n++] = (Mutator)m;
  }

  return n > 0 ? eligible[rngBelow(&rng, n)] : MUTATOR_NONE;
}

int startRunWithSeed(Uint32 seed, bool daily)
{
  memset(&run, 0, sizeof(run));

  run.active = true;
  run.daily = daily;
  run.seed = seed;
  run.dailyDate = daily ? (int)seed : 0;
  run.checkpoint = -1;

  rngSeed(&run.levelRng, seed, STREAM_LEVELS);

  run.stage = 0;
  run.level = drawLevel(0);
  run.mutator = drawMutator(0, run.level);

  return run.level;
}

int startRun(bool daily)
{
  if (daily)
  {
    return startRunWithSeed((Uint32)todayStamp(), true);
  }

  // Two draws, because RAND_MAX is only guaranteed to be 32767.
  Uint32 seed = ((Uint32)rand() << 16) ^ (Uint32)rand() ^ ((Uint32)rand() << 8);

  return startRunWithSeed(seed, false);
}

bool runActive(void)
{
  return run.active;
}

bool runIsDaily(void)
{
  return run.active && run.daily;
}

Uint32 runSeed(void)
{
  return run.seed;
}

int runStage(void)
{
  return run.stage;
}

int runAct(void)
{
  return run.stage / RUN_STAGES_PER_ACT;
}

int runStageInAct(void)
{
  return run.stage % RUN_STAGES_PER_ACT;
}

bool runStageIsBoss(void)
{
  return stageIsBoss(run.stage);
}

int runLevel(void)
{
  return run.level;
}

bool runAtFinale(void)
{
  return run.active && runAct() == RUN_ACTS - 1 && runStageIsBoss();
}

int runClaimVictory(void)
{
  if (!run.active || run.won)
  {
    return 0;
  }

  run.won = true;

  return runScoreValue(RUN_VICTORY_BONUS);
}

bool runWon(void)
{
  return run.active && run.won;
}

void runForceLevel(int level)
{
  if (!run.active || level < 0 || level >= levelCount())
  {
    return;
  }

  run.level = level;
  run.used[level] = true;
  run.mutator = MUTATOR_NONE;
}

int runAdvance(void)
{
  run.stage++;
  run.level = drawLevel(run.stage);
  run.mutator = drawMutator(run.stage, run.level);

  return run.level;
}

Mutator runMutator(void)
{
  return run.active ? run.mutator : MUTATOR_NONE;
}

// ---------------------------------------------------------------------------
// Perks and curses
// ---------------------------------------------------------------------------

bool runHasPerk(Perk perk)
{
  return run.active && (unsigned)perk < (unsigned)PERK_COUNT &&
         (run.perks & PERK_BIT(perk)) != 0;
}

int runPerkCount(void)
{
  int count = 0;

  for (int i = 0; i < PERK_COUNT; i++)
  {
    if (run.perks & PERK_BIT(i))
    {
      count++;
    }
  }

  return count;
}

int runOfferPerks(Perk out[RUN_MAX_OFFERS], int lives)
{
  Perk eligible[PERK_COUNT];
  int n = 0;

  for (int i = 0; i < PERK_COUNT; i++)
  {
    const PerkInfo *info = perkInfo((Perk)i);

    if (info->instant)
    {
      if (i == PERK_EXTRA_LIFE && lives >= 9)
      {
        continue;
      }
    }
    else if ((run.perks & PERK_BIT(i)) || (!run.daily && !perkUnlocked((Perk)i)))
    {
      continue;
    }

    eligible[n++] = (Perk)i;
  }

  Rng rng;
  rngSeed(&rng, run.seed, STREAM_PERKS + (Uint64)run.stage);

  int count = n < RUN_MAX_OFFERS ? n : RUN_MAX_OFFERS;

  // The first `count` steps of a Fisher-Yates shuffle: three distinct perks,
  // each equally likely.
  for (int i = 0; i < count; i++)
  {
    int j = i + rngBelow(&rng, n - i);
    Perk swap = eligible[i];

    eligible[i] = eligible[j];
    eligible[j] = swap;
    out[i] = eligible[i];
  }

  return count;
}

void runTakePerk(Perk perk)
{
  if (!run.active || (unsigned)perk >= (unsigned)PERK_COUNT || perkInfo(perk)->instant)
  {
    return;
  }

  run.perks |= PERK_BIT(perk);
}

bool runHasCurse(Curse curse)
{
  return run.active && (unsigned)curse < (unsigned)CURSE_COUNT &&
         (run.curses & (1u << (unsigned)curse)) != 0;
}

int runCurseCount(void)
{
  int count = 0;

  for (int i = 0; i < CURSE_COUNT; i++)
  {
    if (runHasCurse((Curse)i))
    {
      count++;
    }
  }

  return count;
}

Curse runNextCurse(void)
{
  Curse remaining[CURSE_COUNT];
  int n = 0;

  for (int i = 0; i < CURSE_COUNT; i++)
  {
    if (!runHasCurse((Curse)i))
    {
      remaining[n++] = (Curse)i;
    }
  }

  if (n == 0)
  {
    return CURSE_COUNT;
  }

  Rng rng;
  rngSeed(&rng, run.seed, STREAM_CURSES + (Uint64)runAct());

  return remaining[rngBelow(&rng, n)];
}

void runGoDeeper(int score)
{
  if (!run.active)
  {
    return;
  }

  Curse curse = runNextCurse();

  if (curse != CURSE_COUNT)
  {
    run.curses |= 1u << (unsigned)curse;
  }

  run.descents++;
  run.checkpoint = score;
}

// ---------------------------------------------------------------------------
// Scoring and tuning
// ---------------------------------------------------------------------------

float runScoreMultiplier(void)
{
  return run.active ? 1.0f + DESCENT_MULTIPLIER * run.descents : 1.0f;
}

float runNextScoreMultiplier(void)
{
  return runScoreMultiplier() + DESCENT_MULTIPLIER;
}

int runScoreValue(int points)
{
  if (!run.active)
  {
    return points;
  }

  return (int)SDL_lroundf((float)points * runScoreMultiplier());
}

int runPointsAtRisk(int score)
{
  if (!run.active || run.checkpoint < 0 || score <= run.checkpoint)
  {
    return 0;
  }

  return (score - run.checkpoint) / 2;
}

float runBallSpeedScale(void)
{
  float scale = 1.0f;

  if (runHasPerk(PERK_GLASS_CANNON))
  {
    scale *= 1.15f;
  }

  if (runHasCurse(CURSE_OVERDRIVE))
  {
    scale *= 1.12f;
  }

  return scale;
}

int runComboCap(void)
{
  return runHasPerk(PERK_GLASS_CANNON) ? 8 : 5;
}

float runItemDropScale(void)
{
  float scale = 1.0f;

  if (runHasPerk(PERK_LUCKY))
  {
    scale *= 1.5f;
  }

  if (runMutator() == MUTATOR_SURGE)
  {
    scale *= 2.0f;
  }

  if (runHasCurse(CURSE_DROUGHT))
  {
    scale *= 0.6f;
  }

  return scale;
}

float runPowerUpDurationScale(void)
{
  return runHasPerk(PERK_OVERTIME) ? 1.5f : 1.0f;
}

float runPaddleWidthScale(void)
{
  return runHasCurse(CURSE_NARROW) ? 0.8f : 1.0f;
}

float runEnemyRespawnScale(void)
{
  float scale = 1.0f;

  if (runMutator() == MUTATOR_SWARM)
  {
    scale /= 3.0f;
  }

  if (runHasCurse(CURSE_HUNTED))
  {
    scale *= 0.5f;
  }

  return scale;
}

int runSpeedLevel(int level)
{
  // The levels were drawn to climb over twenty-seven files in four worlds, and
  // a run climbs four stages an act, so a stage is worth a level and three
  // quarters of that curve.
  return run.active ? run.stage * 7 / 4 : level;
}

void runLevelStarted(void)
{
  run.safetyNetUsed = false;
}

bool runUseSafetyNet(void)
{
  if (!runHasPerk(PERK_SAFETY_NET) || run.safetyNetUsed)
  {
    return false;
  }

  run.safetyNetUsed = true;

  return true;
}

// ---------------------------------------------------------------------------
// The level as played
// ---------------------------------------------------------------------------

static char toughen(char c)
{
  return c == 'B' ? 'D' : (c == 'D' ? 'T' : c);
}

static char weaken(char c)
{
  return c == 'T' ? 'D' : (c == 'D' ? 'B' : c);
}

const Level *levelAsPlayed(int index)
{
  const Level *level = getLevel(index);

  if (!run.active || index != run.level)
  {
    return level;
  }

  Mutator mutator = run.mutator;
  bool hardened = runHasCurse(CURSE_HARDENED);
  bool reshapes = mutator == MUTATOR_MIRRORED || mutator == MUTATOR_ARMORED ||
                  mutator == MUTATOR_BRITTLE || mutator == MUTATOR_GOLD_RUSH;

  if (!reshapes && !hardened)
  {
    return level;
  }

  shapedLevel = *level;

  for (int row = 0; row < LEVEL_PATTERN_ROWS; row++)
  {
    char *cells = &shapedLevel.pattern[row * LEVEL_PATTERN_COLS];

    if (mutator == MUTATOR_MIRRORED)
    {
      for (int col = 0; col < LEVEL_PATTERN_COLS / 2; col++)
      {
        char swap = cells[col];

        cells[col] = cells[LEVEL_PATTERN_COLS - 1 - col];
        cells[LEVEL_PATTERN_COLS - 1 - col] = swap;
      }
    }

    for (int col = 0; col < LEVEL_PATTERN_COLS; col++)
    {
      char c = cells[col];

      // The curse first and the mutator after it, so a BRITTLE level in a
      // HARDENED run gives back what the curse took.
      if (hardened && c == 'B')
      {
        c = 'D';
      }

      switch (mutator)
      {
      case MUTATOR_ARMORED:
        c = toughen(c);
        break;
      case MUTATOR_BRITTLE:
        c = weaken(c);
        break;
      case MUTATOR_GOLD_RUSH:
        if (c == 'B' && (row * 7 + col * 3) % 5 == 0)
        {
          c = 'G';
        }
        break;
      default:
        break;
      }

      cells[col] = c;
    }
  }

  return &shapedLevel;
}

// ---------------------------------------------------------------------------
// The end
// ---------------------------------------------------------------------------

const RunResult *runFinish(RunEnd end, int score)
{
  if (!run.active)
  {
    return &lastResult;
  }

  // Stopping on purpose - cashing out, or finishing after the finale - keeps
  // everything; only dying or walking away pays the price of going deeper.
  bool stopped = end == RUN_END_CASHED || end == RUN_END_WON;
  int lost = stopped ? 0 : runPointsAtRisk(score);
  int final = score - lost > 0 ? score - lost : 0;

  lastResult = (RunResult){
      .end = end,
      .won = run.won,
      .daily = run.daily,
      .dailyDate = run.dailyDate,
      .score = final,
      .lost = lost,
      .act = runAct() + 1,
      .stageInAct = runStageInAct() + 1,
      .perks = runPerkCount(),
  };

  // recordScore() writes the save itself when the run makes the table.
  lastResult.rank = recordScore(final, lastResult.act);

  bool changed = false;

  // Only today's run, and only a run of a day at least as new as the one the
  // save already holds: a daily begun just before midnight and finished after
  // it is still that day's, and must not overwrite the next one.
  if (run.daily && run.dailyDate > 0)
  {
    if (run.dailyDate > saveData.dailyDate)
    {
      saveData.dailyDate = run.dailyDate;
      saveData.dailyBest = final;
      lastResult.newDailyBest = final > 0;
      changed = true;
    }
    else if (run.dailyDate == saveData.dailyDate && final > saveData.dailyBest)
    {
      saveData.dailyBest = final;
      lastResult.newDailyBest = true;
      changed = true;
    }
  }

  run.active = false;

  if (changed)
  {
    writeSave();
  }

  return &lastResult;
}

const RunResult *runLastResult(void)
{
  return &lastResult;
}

void runPreviewResult(RunEnd end, int score)
{
  bool won = end == RUN_END_WON;

  lastResult = (RunResult){
      .end = end,
      .won = won,
      .score = score > 0 ? score : 0,
      .act = won ? RUN_ACTS : 2,
      .stageInAct = won ? RUN_STAGES_PER_ACT : 2,
      .perks = won ? 11 : 4,
      .rank = -1,
  };
}
