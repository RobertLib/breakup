// The levels as they actually ship, and the tables that map a level index to a
// world and a music track.
//
// Most of this is a check on the data rather than on the code: a level file is
// a plain text file and a documented mod point, and the parser is deliberately
// forgiving - an unrecognised character becomes empty space and a nameless
// level becomes "LEVEL". Forgiving means a broken level loads rather than
// failing, so nothing else in the game will ever complain about one. This is
// where it gets complained about.
//
// The structural assertions are written to hold for any set of levels, not
// just the twenty-seven in the tree, because dropping another level file in is
// a thing the README tells people to do.

#include "test.h"

#include "../src/globals.h"
#include "../src/types.h"
#include "../src/level-types.h"
#include "../src/level-manager.h"
#include "../src/lib/audio.h"

static bool isValidPatternChar(char c)
{
  return c == '_' || c == 'B' || c == 'D' || c == 'T' || c == 'S' ||
         c == 'X' || c == 'G' || c == 'F' || c == 'E' || c == 'V' ||
         c == 'W' || c == 'U';
}

static bool isDestructible(char c)
{
  return c == 'B' || c == 'D' || c == 'T' || c == 'X' || c == 'G' || c == 'F';
}

static void testLevelsLoad(void)
{
  TEST_GROUP("levels: the assets embedded and parsed");

  // A build whose assets did not embed is a build that starts to an empty
  // field and says nothing. embed_assets.sh and the makefile between them are
  // supposed to make that impossible; this is the assertion that it worked.
  const int count = getNumberOfLevels();

  CHECK(count > 0);

  if (count == 0)
  {
    printf("  (no levels loaded - every check below is skipped)\n");
    return;
  }

  for (int i = 0; i < count; i++)
  {
    const Level *level = getLevel(i);

    CHECK(level != NULL);

    // Every level is named, because the HUD, the banner, the level select
    // grid and the game over screen all print it. renderTextBlended() turns an
    // empty string into a space rather than exiting, but a blank banner is
    // still a level nobody can identify.
    CHECK(level->name[0] != '\0');
    CHECK(strlen(level->name) < LEVEL_NAME_MAX);

    // The parser writes '_' for anything it does not recognise, so a character
    // outside this set means the parser let something through rather than
    // meaning a level file is odd.
    for (int cell = 0; cell < LEVEL_PATTERN_LENGTH; cell++)
    {
      if (!isValidPatternChar(level->pattern[cell]))
      {
        CHECK_INT(level->pattern[cell], '_');
        break;
      }
    }

    CHECK(level->boss >= 0);
  }
}

static void testLevelObjectives(void)
{
  TEST_GROUP("levels: every one of them can actually be finished");

  const int count = getNumberOfLevels();

  for (int i = 0; i < count; i++)
  {
    const Level *level = getLevel(i);
    int destructible = 0;
    int crystals = 0;

    for (int cell = 0; cell < LEVEL_PATTERN_LENGTH; cell++)
    {
      char c = level->pattern[cell];

      if (isDestructible(c))
      {
        destructible++;
      }
      if (c == 'F')
      {
        crystals++;
      }
    }

    if (isBossLevel(i))
    {
      // A boss level ends when the boss does, so its bricks are cover rather
      // than the objective - but an arena with nothing in it is a fight with
      // no power-ups in it, and a crystal here would be an objective that
      // competes with the boss for what ends the level.
      CHECK(crystals == 0);
    }
    else
    {
      // checkLevelComplete() ends an ordinary level when nothing destructible
      // is left standing, so a level with nothing in it completes on the frame
      // it starts - before the player has touched anything.
      CHECK(destructible > 0);
    }
  }
}

static void testOutOfRangeLevels(void)
{
  TEST_GROUP("levels: an index that does not exist");

  // getLevel() promises never to hand back NULL, because paddle.level arrives
  // from a save file, from an environment variable and from a grid, and each
  // of those used to index the array directly.
  const int count = getNumberOfLevels();
  const int bad[] = {-1, -1000, count, count + 1, 100000};

  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
  {
    const Level *level = getLevel(bad[i]);

    CHECK(level != NULL);
    CHECK_STR(level->name, "LEVEL");
    CHECK_INT(level->boss, 0);

    // The fallback has to be an empty field rather than zeroed memory: '\0' is
    // not a pattern character and would build nothing, but it is also not what
    // the rest of the game reads as empty.
    CHECK_INT(level->pattern[0], '_');
    CHECK_INT(level->pattern[LEVEL_PATTERN_LENGTH - 1], '_');

    // And the queries that take the same index have to answer rather than
    // index off the end of their own tables.
    CHECK_INT(worldForLevel(bad[i]), 0);
    CHECK_INT(levelInWorld(bad[i]), 0);
    CHECK(!isBossLevel(bad[i]));
    CHECK(getLevelContentBottom(bad[i]) > 0);
  }
}

static void testWorldAssignment(void)
{
  TEST_GROUP("worlds: five hand-designed levels each, bosses in between");

  const int count = getNumberOfLevels();
  int regular = 0;
  int perWorld[WORLD_COUNT] = {0};

  for (int i = 0; i < count; i++)
  {
    int world = worldForLevel(i);
    int inWorld = levelInWorld(i);

    // Every level belongs to a world that has a theme and a nebula texture:
    // worldThemes[] and texNebula[] are WORLD_COUNT + 1 long and the extra one
    // is the menu's.
    CHECK(world >= 0 && world < WORLD_COUNT);

    if (isBossLevel(i))
    {
      // A boss has no number inside its world - the HUD prints BOSS instead -
      // and it must not consume one, or the worlds would stop being the five
      // levels each they were drawn as.
      CHECK_INT(inWorld, 0);
    }
    else
    {
      CHECK(inWorld >= 1 && inWorld <= LEVELS_PER_WORLD);
      perWorld[world]++;
      regular++;
    }
  }

  // Only asserted for the shape the game is designed around. Adding a
  // twenty-first ordinary level is allowed - assignWorlds() piles the extras
  // into the last world on purpose - and should not fail a test.
  if (regular == WORLD_COUNT * LEVELS_PER_WORLD)
  {
    for (int w = 0; w < WORLD_COUNT; w++)
    {
      CHECK_INT(perWorld[w], LEVELS_PER_WORLD);
    }
  }

  // The worlds have to start at the beginning and run in order; a level in
  // world 2 before any level in world 1 would mean the backgrounds jump about.
  int highest = 0;

  for (int i = 0; i < count; i++)
  {
    int world = worldForLevel(i);

    CHECK(world <= highest + 1);

    if (world > highest)
    {
      highest = world;
    }
  }
}

static void testBossNumbering(void)
{
  TEST_GROUP("bosses: each fight is a different one");

  const int count = getNumberOfLevels();
  int seen[64] = {0};
  int bosses = 0;

  for (int i = 0; i < count; i++)
  {
    if (!isBossLevel(i))
    {
      continue;
    }

    int number = getLevel(i)->boss;

    bosses++;

    // isBossLevel() is `boss > 0`, and musicForLevel() subtracts one from it.
    CHECK(number > 0);

    if (number > 0 && number < 64)
    {
      // Two levels claiming the same boss would be the same fight twice, with
      // the same name in the banner.
      CHECK_INT(seen[number], 0);
      seen[number]++;
    }
  }

  // The last level should be the one that ends the game.
  if (count > 0)
  {
    CHECK(isBossLevel(count - 1));
  }

  CHECK(bosses > 0);
}

static void testMusicMapping(void)
{
  TEST_GROUP("music: every level lands on a track that exists");

  const int count = getNumberOfLevels();

  for (int i = 0; i < count; i++)
  {
    MusicTrack track = musicForLevel(i);

    // musicAudio[] is MUSIC_COUNT long and playMusic() indexes it with this.
    CHECK(track >= 0 && track < MUSIC_COUNT);

    if (isBossLevel(i))
    {
      CHECK(track >= MUSIC_BOSS_FIRST && track < MUSIC_COUNT);
    }
    else
    {
      // An ordinary level gets its own world's set, which is the fix for the
      // bosses shifting the phase and leaving a world playing one track on
      // four of its five levels.
      int world = worldForLevel(i);
      int first = MUSIC_GAME_FIRST + world * GAME_TRACKS_PER_WORLD;

      CHECK(track >= first && track < first + GAME_TRACKS_PER_WORLD);
    }
  }

  // The same guarantee for an index that does not exist: it picks something
  // rather than reading off the end of the table.
  const int bad[] = {-1, count, count + 500};

  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
  {
    MusicTrack track = musicForLevel(bad[i]);
    CHECK(track >= 0 && track < MUSIC_COUNT);
  }

  // Each world walks through its four tracks rather than repeating one, which
  // is what GAME_TRACKS_PER_WORLD against LEVELS_PER_WORLD is for.
  for (int world = 0; world < WORLD_COUNT; world++)
  {
    int distinct = 0;
    int seen[GAME_TRACKS_PER_WORLD] = {0};

    for (int i = 0; i < count; i++)
    {
      if (isBossLevel(i) || worldForLevel(i) != world)
      {
        continue;
      }

      int first = MUSIC_GAME_FIRST + world * GAME_TRACKS_PER_WORLD;
      int variant = musicForLevel(i) - first;

      if (variant >= 0 && variant < GAME_TRACKS_PER_WORLD && !seen[variant])
      {
        seen[variant] = 1;
        distinct++;
      }
    }

    if (distinct > 0)
    {
      CHECK_INT(distinct, GAME_TRACKS_PER_WORLD);
    }
  }
}

static void testContentBottom(void)
{
  TEST_GROUP("level geometry: where the paddle is parked");

  const int count = getNumberOfLevels();

  for (int i = 0; i < count; i++)
  {
    int bottom = getLevelContentBottom(i);

    // The paddle sits PADDLE_CONTENT_GAP below this and the camera is placed
    // from it, so a nonsense answer is a level that starts with the field off
    // the screen.
    CHECK(bottom > 0);
    CHECK(bottom <= LEVEL_HEIGHT);
  }
}

void testLevels(void)
{
  initializeLevelManager();

  testLevelsLoad();
  testLevelObjectives();
  testOutOfRangeLevels();
  testWorldAssignment();
  testBossNumbering();
  testMusicMapping();
  testContentBottom();

  destroyLevelManager();
}
