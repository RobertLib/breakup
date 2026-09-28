// Run mode: its random number generator, the shape of a run, the scoring that
// going deeper changes, the perks on offer and what unlocks them.
//
// Most of what a run is can be checked without a window, because it is a
// function of a seed: which level each stage draws, which mutator reshapes it,
// which three perks are on the table and which curse the next descent adds.
// That is also the part that has to be right for a daily run to mean anything
// - two players of the same date must be handed the same run - so the first
// group below pins the generator to the reference implementation's output
// rather than merely to itself.
//
// Runs with setSaveScripted(true), like every test that can reach writeSave():
// runFinish() records bests, and a perk unlocking writes the save on the spot.

#include "test.h"

#include "../src/globals.h"
#include "../src/types.h"
#include "../src/level-manager.h"
#include "../src/lib/save.h"
#include "../src/balls/balls.h"
#include "../src/paddle/paddle.h"
#include "../src/run/perks.h"
#include "../src/run/rng.h"
#include "../src/run/run.h"

// Enough stages to reach the fifth act, where the pools open up.
#define STAGES 20

static void testRng(void)
{
  TEST_GROUP("rng: PCG32, output for output with the reference");

  // pcg32-demo.c from pcg-random.org, seeded with pcg32_srandom(42, 54).
  static const Uint32 expected[6] = {0xa15c02b7, 0x7b47f409, 0xba1d3330,
                                     0x83d2f293, 0xbfa4784b, 0xcbed606e};
  Rng rng;
  rngSeed(&rng, 42, 54);

  for (int i = 0; i < 6; i++)
  {
    CHECK_INT(rngNext(&rng), expected[i]);
  }

  // Bounded draws stay in bounds and reach both ends.
  rngSeed(&rng, 7, 1);
  bool sawLow = false, sawHigh = false, inRange = true;

  for (int i = 0; i < 2000; i++)
  {
    int v = rngBelow(&rng, 7);
    inRange = inRange && v >= 0 && v < 7;
    sawLow = sawLow || v == 0;
    sawHigh = sawHigh || v == 6;
  }

  CHECK(inRange);
  CHECK(sawLow);
  CHECK(sawHigh);
  CHECK_INT(rngBelow(&rng, 1), 0);
  CHECK_INT(rngBelow(&rng, 0), 0);
  CHECK_INT(rngBelow(&rng, -5), 0);
}

typedef struct Plan
{
  int level[STAGES];
  Mutator mutator[STAGES];
} Plan;

// A run walked the way BREAKUP_RUN_STAGE walks one: going deeper at every boss.
static Plan planFor(Uint32 seed)
{
  Plan plan;

  plan.level[0] = startRunWithSeed(seed, false);
  plan.mutator[0] = runMutator();

  for (int stage = 1; stage < STAGES; stage++)
  {
    if (runStageIsBoss())
    {
      runGoDeeper(0);
    }

    plan.level[stage] = runAdvance();
    plan.mutator[stage] = runMutator();
  }

  runFinish(RUN_END_QUIT, 0);

  return plan;
}

static void testDeterminism(void)
{
  TEST_GROUP("run: a seed is a run, the same one every time");

  Plan a = planFor(1234);
  Plan b = planFor(1234);
  bool same = true;

  for (int i = 0; i < STAGES; i++)
  {
    same = same && a.level[i] == b.level[i] && a.mutator[i] == b.mutator[i];
  }

  CHECK(same);

  // And a different seed is (almost always) a different run. Over five seeds
  // at least one must differ from the first, or the seed is not being used.
  bool anyDiffers = false;

  for (Uint32 seed = 1; seed <= 5; seed++)
  {
    Plan other = planFor(seed * 7919);

    for (int i = 0; i < STAGES; i++)
    {
      anyDiffers = anyDiffers || other.level[i] != a.level[i];
    }
  }

  CHECK(anyDiffers);
}

static void testShape(void)
{
  TEST_GROUP("run: three levels and a boss an act, drawn from the right pools");

  if (getNumberOfLevels() != 27)
  {
    printf("  (not the shipped level set - skipped)\n");
    return;
  }

  for (Uint32 seed = 100; seed < 140; seed++)
  {
    Plan plan = planFor(seed);
    bool used[100] = {false};
    bool shapeOk = true, poolOk = true, noRepeats = true, mutatorsOk = true;

    for (int stage = 0; stage < STAGES; stage++)
    {
      int level = plan.level[stage];
      int act = stage / RUN_STAGES_PER_ACT;
      bool boss = stage % RUN_STAGES_PER_ACT == RUN_STAGES_PER_ACT - 1;

      shapeOk = shapeOk && level >= 0 && level < 27 && isBossLevel(level) == boss;

      if (boss && act < 4)
      {
        static const int lo[4] = {1, 3, 5, 7}, hi[4] = {2, 4, 6, 7};
        int number = getLevel(level)->boss;
        poolOk = poolOk && number >= lo[act] && number <= hi[act];
      }
      else if (!boss && act < 4)
      {
        poolOk = poolOk && worldForLevel(level) == act;
      }

      // Sixteen stages fit in the shipped pools without a repeat: three of
      // five levels a world, one of two bosses a tier.
      if (stage < 16)
      {
        noRepeats = noRepeats && !used[level];
        used[level] = true;
      }

      // No mutator in the first act, and never on a boss.
      if (act == 0 || boss)
      {
        mutatorsOk = mutatorsOk && plan.mutator[stage] == MUTATOR_NONE;
      }
    }

    CHECK(shapeOk);
    CHECK(poolOk);
    CHECK(noRepeats);
    CHECK(mutatorsOk);
  }
}

// Finds a seed and a stage that drew `mutator`, and leaves the run parked on
// it. False if nothing in the search drew it.
static bool findMutator(Mutator mutator)
{
  for (Uint32 seed = 1; seed < 400; seed++)
  {
    startRunWithSeed(seed, false);

    for (int stage = 1; stage < 12; stage++)
    {
      if (runStageIsBoss())
      {
        runGoDeeper(0);
      }

      runAdvance();

      if (runMutator() == mutator)
      {
        return true;
      }
    }

    runFinish(RUN_END_QUIT, 0);
  }

  return false;
}

static void testMutators(void)
{
  TEST_GROUP("run: what a mutator does to the pattern it is played on");

  // Outside a run the level is the level, the very same one.
  CHECK(levelAsPlayed(0) == getLevel(0));

  bool found = findMutator(MUTATOR_MIRRORED);
  CHECK(found);

  if (found)
  {
    const Level *original = getLevel(runLevel());
    const Level *played = levelAsPlayed(runLevel());
    bool mirrored = true;

    for (int row = 0; row < LEVEL_PATTERN_ROWS; row++)
    {
      for (int col = 0; col < LEVEL_PATTERN_COLS; col++)
      {
        char was = original->pattern[row * LEVEL_PATTERN_COLS + LEVEL_PATTERN_COLS - 1 - col];
        char is = played->pattern[row * LEVEL_PATTERN_COLS + col];

        // HARDENED may be on by now, which turns B into D on top of the flip.
        mirrored = mirrored && (is == was || (was == 'B' && is == 'D'));
      }
    }

    CHECK(mirrored);

    // Any other level is untouched: the reshaping is for the stage in play.
    int other = runLevel() == 0 ? 1 : 0;
    CHECK(levelAsPlayed(other) == getLevel(other));

    runFinish(RUN_END_QUIT, 0);
  }

  found = findMutator(MUTATOR_ARMORED);
  CHECK(found);

  if (found)
  {
    const Level *original = getLevel(runLevel());
    const Level *played = levelAsPlayed(runLevel());
    bool armored = true;

    // ARMORED is never drawn under HARDENED, so the map is exact.
    CHECK(!runHasCurse(CURSE_HARDENED));

    for (int i = 0; i < LEVEL_PATTERN_LENGTH; i++)
    {
      char was = original->pattern[i];
      char want = was == 'B' ? 'D' : (was == 'D' ? 'T' : was);
      armored = armored && played->pattern[i] == want;
    }

    CHECK(armored);
    runFinish(RUN_END_QUIT, 0);
  }

  found = findMutator(MUTATOR_SWARM);

  if (found)
  {
    const Level *level = getLevel(runLevel());
    bool spawners = false;

    for (int i = 0; i < LEVEL_PATTERN_LENGTH; i++)
    {
      char c = level->pattern[i];
      spawners = spawners || c == 'E' || c == 'V' || c == 'W' || c == 'U';
    }

    CHECK(spawners);
    CHECK_NEAR(runEnemyRespawnScale(), runHasCurse(CURSE_HUNTED) ? 1.0 / 6.0 : 1.0 / 3.0, 1e-6);
    runFinish(RUN_END_QUIT, 0);
  }
}

static void testScoring(void)
{
  TEST_GROUP("run: the multiplier, the checkpoint and what dying costs");

  loadSaveFromText("", 0);

  CHECK_INT(runScoreValue(300), 300);
  CHECK_NEAR(runScoreMultiplier(), 1.0, 1e-6);

  startRunWithSeed(99, false);
  CHECK_INT(runScoreValue(300), 300);
  CHECK_INT(runPointsAtRisk(5000), 0); // nothing is at risk before a descent

  runGoDeeper(10000);
  CHECK_NEAR(runScoreMultiplier(), 1.5, 1e-6);
  CHECK_INT(runScoreValue(300), 450);
  CHECK_INT(runPointsAtRisk(10000), 0);
  CHECK_INT(runPointsAtRisk(14000), 2000);
  CHECK_INT(runCurseCount(), 1);

  const RunResult *died = runFinish(RUN_END_DIED, 14000);
  CHECK_INT(died->score, 12000);
  CHECK_INT(died->lost, 2000);
  CHECK_INT(died->rank, 0);
  CHECK_INT(saveData.highScores[0], 12000);
  CHECK_INT(saveData.highActs[0], 1);
  CHECK(!died->won);
  CHECK(!runActive());

  // Cashing out keeps everything.
  startRunWithSeed(99, false);
  runGoDeeper(10000);
  const RunResult *cashed = runFinish(RUN_END_CASHED, 14000);
  CHECK_INT(cashed->score, 14000);
  CHECK_INT(cashed->lost, 0);
  CHECK_INT(cashed->rank, 0);
  CHECK_INT(saveData.highScores[0], 14000);
  CHECK_INT(saveData.highScores[1], 12000);

  // A worse run goes into the table underneath, and says where.
  startRunWithSeed(99, false);
  CHECK_INT(runFinish(RUN_END_DIED, 100)->rank, 2);
  CHECK_INT(saveData.highScores[0], 14000);

  // A run worth nothing is not in the table at all.
  startRunWithSeed(99, false);
  CHECK_INT(runFinish(RUN_END_QUIT, 0)->rank, -1);

  // A daily keeps its own best, per date.
  startRunWithSeed(20260928, true);
  const RunResult *daily = runFinish(RUN_END_CASHED, 3000);
  CHECK(daily->daily);
  CHECK(daily->newDailyBest);
  CHECK_INT(saveData.dailyDate, 20260928);
  CHECK_INT(saveData.dailyBest, 3000);

  startRunWithSeed(20260928, true);
  CHECK(!runFinish(RUN_END_DIED, 2000)->newDailyBest);
  CHECK_INT(saveData.dailyBest, 3000);

  // An older day's run cannot overwrite a newer day's best.
  startRunWithSeed(20260927, true);
  runFinish(RUN_END_CASHED, 90000);
  CHECK_INT(saveData.dailyDate, 20260928);
  CHECK_INT(saveData.dailyBest, 3000);

  // After all five curses, going deeper still raises the multiplier.
  startRunWithSeed(5, false);

  for (int i = 0; i < CURSE_COUNT; i++)
  {
    Curse next = runNextCurse();
    CHECK(next != CURSE_COUNT && !runHasCurse(next));
    runGoDeeper(0);
    CHECK(runHasCurse(next));
  }

  CHECK_INT(runNextCurse(), CURSE_COUNT);
  runGoDeeper(0);
  CHECK_NEAR(runScoreMultiplier(), 1.0 + 0.5 * (CURSE_COUNT + 1), 1e-6);
  CHECK_NEAR(runPaddleWidthScale(), 0.8, 1e-6);
  CHECK_NEAR(runItemDropScale(), 0.6, 1e-6);
  runFinish(RUN_END_QUIT, 0);

  loadSaveFromText("", 0);
}

static void testFinale(void)
{
  TEST_GROUP("run: four acts, and The Sovereign at the end of them wins it");

  loadSaveFromText("", 0);

  startRunWithSeed(31337, false);

  // Walked to the last stage of the fourth act, going deeper at every boss on
  // the way; none of those is the finale.
  bool finaleEarly = false;

  for (int stage = 0; stage < RUN_ACTS * RUN_STAGES_PER_ACT - 1; stage++)
  {
    finaleEarly = finaleEarly || runAtFinale();

    if (runStageIsBoss())
    {
      runGoDeeper(0);
    }

    runAdvance();
  }

  CHECK(!finaleEarly);
  CHECK(runAtFinale());
  CHECK(runStageIsBoss());
  CHECK_INT(runAct(), RUN_ACTS - 1);

  if (getNumberOfLevels() == 27)
  {
    CHECK_INT(getLevel(runLevel())->boss, 7); // The Sovereign
  }

  // The bonus is paid once, at the multiplier three descents have built.
  CHECK(!runWon());
  CHECK_INT(runClaimVictory(), (int)(RUN_VICTORY_BONUS * 2.5f));
  CHECK(runWon());
  CHECK_INT(runClaimVictory(), 0);

  // Finishing there is a win; so, later, is cashing out of the endless acts.
  const RunResult *won = runFinish(RUN_END_WON, 50000);
  CHECK(won->won);
  CHECK_INT(won->lost, 0);
  CHECK_INT(won->score, 50000);
  CHECK_INT(won->act, RUN_ACTS);

  startRunWithSeed(31337, false);

  for (int stage = 0; stage < RUN_ACTS * RUN_STAGES_PER_ACT - 1; stage++)
  {
    if (runStageIsBoss())
    {
      runGoDeeper(0);
    }

    runAdvance();
  }

  runClaimVictory();
  runGoDeeper(1000);
  runAdvance();

  // Past the finale, the fifth act is not a second one.
  bool finaleAgain = false;

  for (int stage = 0; stage < RUN_STAGES_PER_ACT; stage++)
  {
    finaleAgain = finaleAgain || runAtFinale();
    runAdvance();
  }

  CHECK(!finaleAgain);
  CHECK(runFinish(RUN_END_CASHED, 3000)->won);

  // A run that never got there has not won.
  startRunWithSeed(31337, false);
  CHECK(!runFinish(RUN_END_DIED, 10)->won);

  // BREAKUP_LEVEL's hook: the stage plays the file asked for, plainly.
  startRunWithSeed(8, false);
  runForceLevel(4);
  CHECK_INT(runLevel(), 4);
  CHECK_INT(runMutator(), MUTATOR_NONE);
  runForceLevel(-3);
  CHECK_INT(runLevel(), 4);
  runFinish(RUN_END_QUIT, 0);

  loadSaveFromText("", 0);
}

static void testOffers(void)
{
  TEST_GROUP("run: three different perks on offer, only ones the save has");

  loadSaveFromText("", 0);

  startRunWithSeed(4242, false);

  Perk first[RUN_MAX_OFFERS];
  int count = runOfferPerks(first, 3);
  CHECK_INT(count, RUN_MAX_OFFERS);

  bool distinct = first[0] != first[1] && first[1] != first[2] && first[0] != first[2];
  CHECK(distinct);

  bool allUnlocked = true;

  for (int i = 0; i < count; i++)
  {
    allUnlocked = allUnlocked && perkUnlocked(first[i]);
  }

  CHECK(allUnlocked);

  // Asking twice is the same offer.
  Perk again[RUN_MAX_OFFERS];
  runOfferPerks(again, 3);
  CHECK(again[0] == first[0] && again[1] == first[1] && again[2] == first[2]);

  // A held perk is not offered again, and nine lives take +1 LIFE off the table.
  bool heldOffered = false, lifeOffered = false;

  for (int stage = 0; stage < 40; stage++)
  {
    Perk offer[RUN_MAX_OFFERS];
    int n = runOfferPerks(offer, 9);

    for (int i = 0; i < n; i++)
    {
      heldOffered = heldOffered || runHasPerk(offer[i]);
      lifeOffered = lifeOffered || offer[i] == PERK_EXTRA_LIFE;
    }

    if (n > 0 && !perkInfo(offer[0])->instant)
    {
      runTakePerk(offer[0]);
    }

    runAdvance();
  }

  CHECK(!heldOffered);
  CHECK(!lifeOffered);

  // Every starter perk eventually taken, and nothing locked ever offered.
  CHECK_INT(runPerkCount(), 8);
  runFinish(RUN_END_QUIT, 0);

  // A daily offers the whole collection, unlocked or not, so that everybody's
  // daily is the same game.
  startRunWithSeed(20260928, true);
  bool sawLocked = false;

  for (int stage = 0; stage < 60 && !sawLocked; stage++)
  {
    Perk offer[RUN_MAX_OFFERS];
    int n = runOfferPerks(offer, 3);

    for (int i = 0; i < n; i++)
    {
      sawLocked = sawLocked || !perkUnlocked(offer[i]);
    }

    runAdvance();
  }

  CHECK(sawLocked);
  runFinish(RUN_END_QUIT, 0);

  loadSaveFromText("", 0);
}

static void testCombo(void)
{
  TEST_GROUP("combo: SHRINK is worth one more, GLASS CANNON lifts the cap");

  PaddleType saved = paddle.type;

  resetCombo();
  combo = 40;
  CHECK_INT(comboMultiplier(), 5);

  paddle.type = PADDLE_TYPE_SHORT;
  CHECK_INT(comboMultiplier(), 6);
  combo = 0;
  CHECK_INT(comboMultiplier(), 2);
  paddle.type = PADDLE_TYPE_DEFAULT;

  startRunWithSeed(1, false);
  runTakePerk(PERK_GLASS_CANNON);
  combo = 40;
  CHECK_INT(comboMultiplier(), 8);
  combo = 100;
  CHECK_INT(comboMultiplier(), 8);
  CHECK_NEAR(runBallSpeedScale(), 1.15, 1e-6);
  runFinish(RUN_END_QUIT, 0);

  // Out of the run the cap is the campaign's again.
  CHECK_INT(comboMultiplier(), 5);
  CHECK_NEAR(runBallSpeedScale(), 1.0, 1e-6);

  resetCombo();
  paddle.type = saved;
  loadSaveFromText("", 0);
}

static void testUnlocks(void)
{
  TEST_GROUP("perks: what unlocks them, and that it sticks");

  loadSaveFromText("", 0);

  CHECK_INT(saveData.perksUnlocked, starterPerks());
  CHECK_INT(unlockedPerkCount(), 8);
  CHECK_INT(perkCollectionSize(), 14);
  CHECK(perkUnlocked(PERK_EXTRA_LIFE));
  CHECK(!perkUnlocked(PERK_GLASS_CANNON));

  // Beating a boss that unlocks nothing unlocks nothing.
  noteBossDefeated(1);
  CHECK_INT(saveData.perksUnlocked, starterPerks());

  noteBossDefeated(2);
  CHECK(perkUnlocked(PERK_GLASS_CANNON));
  noteBossDefeated(5);
  CHECK(perkUnlocked(PERK_INFERNO));
  noteBossDefeated(7);
  CHECK(perkUnlocked(PERK_MELTDOWN));

  // Seven detonations are not eight, and a new level starts the count again.
  noteLevelStarted();
  for (int i = 0; i < 7; i++)
  {
    noteDetonation();
  }
  CHECK(!perkUnlocked(PERK_CHAIN_REACTION));
  noteLevelStarted();
  noteDetonation();
  CHECK(!perkUnlocked(PERK_CHAIN_REACTION));
  for (int i = 0; i < 7; i++)
  {
    noteDetonation();
  }
  CHECK(perkUnlocked(PERK_CHAIN_REACTION));

  noteLevelStarted();
  for (int i = 0; i < 10; i++)
  {
    noteEnemyKilled();
  }
  CHECK(perkUnlocked(PERK_HUNTER));

  noteComboMultiplier(4);
  CHECK(!perkUnlocked(PERK_SPLIT_SHOT));
  noteComboMultiplier(5);
  CHECK(perkUnlocked(PERK_SPLIT_SHOT));

  CHECK_INT(unlockedPerkCount(), 14);
  CHECK_INT(saveData.perksUnlocked, unlockablePerks());

  // Every perk has a name and a description, and every locked one says how.
  bool described = true;

  for (int i = 0; i < PERK_COUNT; i++)
  {
    const PerkInfo *info = perkInfo((Perk)i);
    described = described && info->name[0] != '\0' && info->description[0] != '\0';

    if (!info->instant && !(starterPerks() & PERK_BIT(i)))
    {
      described = described && info->unlockHint != NULL;
    }
  }

  CHECK(described);
  CHECK_STR(perkInfo((Perk)PERK_COUNT)->name, "?");

  loadSaveFromText("", 0);
}

void testRun(void)
{
  setSaveScripted(true);
  initializeLevelManager();

  testRng();
  testDeterminism();
  testShape();
  testMutators();
  testScoring();
  testFinale();
  testOffers();
  testCombo();
  testUnlocks();

  destroyLevelManager();
}
