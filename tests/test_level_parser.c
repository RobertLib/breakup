// The level text parser, fed text directly.
//
// A level file is a documented mod point - "add a level28.txt and it shows up"
// - and the parser is deliberately forgiving, so this is where what it forgives
// and what it keeps is pinned down. parseLevelText() is the parser
// initializeLevelManager() runs each embedded file through, exposed so it can
// be handed a buffer here.

#include "test.h"

#include "../src/globals.h"
#include "../src/level-types.h"
#include "../src/level-manager.h"

#include <stdlib.h>

// The parser must not depend on anything the struct held before, so every
// parse here starts from garbage rather than from zeroes.
static bool parse(Level *level, const char *text)
{
  memset(level, 'Z', sizeof(*level));
  return parseLevelText(level, text, strlen(text));
}

static char cell(const Level *level, int row, int col)
{
  return level->pattern[row * LEVEL_PATTERN_COLS + col];
}

// True when every cell of the pattern from `fromRow` down is '_'.
static bool emptyFrom(const Level *level, int fromRow)
{
  for (int i = fromRow * LEVEL_PATTERN_COLS; i < LEVEL_PATTERN_LENGTH; i++)
  {
    if (level->pattern[i] != '_')
    {
      return false;
    }
  }

  return true;
}

// True when the row reads exactly `expected` (which must be 15 characters).
static bool rowIs(const Level *level, int row, const char *expected)
{
  return strncmp(&level->pattern[row * LEVEL_PATTERN_COLS], expected,
                 LEVEL_PATTERN_COLS) == 0;
}

static void testMinimal(void)
{
  TEST_GROUP("level parser: a name and a few rows");

  Level level;

  CHECK(parse(&level, "# Name\nBBB\n_D_\n"));
  CHECK_STR(level.name, "Name");
  CHECK_INT(level.boss, 0);

  CHECK(rowIs(&level, 0, "BBB____________"));
  CHECK(rowIs(&level, 1, "_D_____________"));
  CHECK(emptyFrom(&level, 2));

  // The name is what follows the '#' with the leading blanks taken off, and
  // only the first '#' line names the level - the rest are comments.
  CHECK(parse(&level, "#\t  Spaced Out\n# not the name\nB\n"));
  CHECK_STR(level.name, "Spaced Out");
  CHECK_INT(cell(&level, 0, 0), 'B');
  CHECK(emptyFrom(&level, 1));

  // A comment line is not a row: the bricks land on row 0 whatever is above.
  CHECK(parse(&level, "# A\n# B\n# C\nBBB\n"));
  CHECK(rowIs(&level, 0, "BBB____________"));
}

static void testBossDirective(void)
{
  TEST_GROUP("level parser: #boss N is a directive, not a name");

  Level level;

  // Before the name line, and after it: the same level either way, and the
  // directive is not a pattern row.
  CHECK(parse(&level, "#boss 3\n# THE BROOD\nBBB\n"));
  CHECK_INT(level.boss, 3);
  CHECK_STR(level.name, "THE BROOD");
  CHECK(rowIs(&level, 0, "BBB____________"));
  CHECK(emptyFrom(&level, 1));

  CHECK(parse(&level, "# THE BROOD\n#boss 3\nBBB\n"));
  CHECK_INT(level.boss, 3);
  CHECK_STR(level.name, "THE BROOD");
  CHECK(rowIs(&level, 0, "BBB____________"));
  CHECK(emptyFrom(&level, 1));

  // The spellings readBossDirective() promises to take: a space, a colon, no
  // separator at all, any case, and the four-digit accumulator cap. None of
  // them is the level's name.
  CHECK(parse(&level, "#boss:1\n"));
  CHECK_INT(level.boss, 1);
  CHECK_STR(level.name, "LEVEL");

  CHECK(parse(&level, "#boss1\n"));
  CHECK_INT(level.boss, 1);

  CHECK(parse(&level, "# BOSS 2\n"));
  CHECK_INT(level.boss, 2);
  CHECK_STR(level.name, "LEVEL");

  CHECK(parse(&level, "#boss 12\n"));
  CHECK_INT(level.boss, 12);

  // The directive as the very last bytes of a file saved without a newline,
  // which is the read off the end of the array the parser's comment is about.
  // The text is bounded by `size`, so the '2' past the end must not be read.
  memset(&level, 'Z', sizeof(level));
  CHECK(parseLevelText(&level, "#boss 72", 7));
  CHECK_INT(level.boss, 7);

  // A boss number that is nonsense has to stay a finite, obviously wrong one:
  // the accumulator stops at four digits rather than overflowing.
  CHECK(parse(&level, "#boss 99999999999999999999\n"));
  CHECK(level.boss > 0);
  CHECK(level.boss <= 99999);
}

static void testBossLookalikes(void)
{
  TEST_GROUP("level parser: a name that merely starts with 'boss'");

  Level level;

  // The number is what makes a directive. Without one, the line is a name -
  // this is the `# Boss Rush` case that used to arrive in the HUD as "LEVEL".
  CHECK(parse(&level, "# Boss Rush\nBBB\n"));
  CHECK_INT(level.boss, 0);
  CHECK_STR(level.name, "Boss Rush");

  CHECK(parse(&level, "# Boss Rush 2\nBBB\n"));
  CHECK_INT(level.boss, 0);
  CHECK_STR(level.name, "Boss Rush 2");

  CHECK(parse(&level, "#boss\nBBB\n"));
  CHECK_INT(level.boss, 0);
  CHECK_STR(level.name, "boss");

  CHECK(parse(&level, "#boss abc\nBBB\n"));
  CHECK_INT(level.boss, 0);
  CHECK_STR(level.name, "boss abc");

  CHECK(parse(&level, "#boss -1\nBBB\n"));
  CHECK_INT(level.boss, 0);
  CHECK_STR(level.name, "boss -1");

  // `#boss 0` is a directive that says "not a boss", and it is still not the
  // name.
  CHECK(parse(&level, "#boss 0\n# Plain\nBBB\n"));
  CHECK_INT(level.boss, 0);
  CHECK_STR(level.name, "Plain");

  // And the keyword in the middle of a row is a row of empties, not a boss.
  CHECK(parse(&level, "# Name\nboss 3\n"));
  CHECK_INT(level.boss, 0);
  CHECK(emptyFrom(&level, 0));
}

static void testRowShapes(void)
{
  TEST_GROUP("level parser: rows too long, too short, too many");

  Level level;

  // Extra cells past the fifteenth are dropped rather than wrapping onto the
  // next row.
  CHECK(parse(&level, "# N\nBBBBBBBBBBBBBBBDDDDD\nT\n"));
  CHECK(rowIs(&level, 0, "BBBBBBBBBBBBBBB"));
  CHECK(rowIs(&level, 1, "T______________"));
  CHECK(emptyFrom(&level, 2));

  // Short rows are padded with '_'. A blank line is *not* a row: the loop
  // that steps over a CRLF pair steps over every run of line breaks, so an
  // empty line between two rows is skipped and the rows close up. An empty
  // row has to be written out as fifteen underscores, which is what every
  // shipped file does.
  CHECK(parse(&level, "BB\n\nD\n"));
  CHECK(rowIs(&level, 0, "BB_____________"));
  CHECK(rowIs(&level, 1, "D______________"));
  CHECK(emptyFrom(&level, 2));

  CHECK(parse(&level, "BB\n_______________\nD\n"));
  CHECK(rowIs(&level, 0, "BB_____________"));
  CHECK(rowIs(&level, 1, "_______________"));
  CHECK(rowIs(&level, 2, "D______________"));
  CHECK(emptyFrom(&level, 3));

  // More rows than the pattern holds: the first LEVEL_PATTERN_ROWS are kept
  // and the rest are ignored, with nothing written past the array.
  const int extra = 10;
  const char *row = "B______________\n";
  const size_t rowLen = strlen(row);
  size_t size = (size_t)(LEVEL_PATTERN_ROWS + extra) * rowLen;
  char *big = malloc(size + 1);

  CHECK(big != NULL);

  if (big != NULL)
  {
    for (int r = 0; r < LEVEL_PATTERN_ROWS + extra; r++)
    {
      memcpy(big + (size_t)r * rowLen, row, rowLen);
    }
    big[size] = '\0';

    // A sentinel after the struct, to see that nothing wrote past it.
    struct
    {
      Level level;
      char sentinel[16];
    } guarded;

    memset(&guarded, 'Z', sizeof(guarded));
    CHECK(parseLevelText(&guarded.level, big, size));

    bool allB = true;
    for (int r = 0; r < LEVEL_PATTERN_ROWS; r++)
    {
      if (cell(&guarded.level, r, 0) != 'B' || cell(&guarded.level, r, 1) != '_')
      {
        allB = false;
      }
    }
    CHECK(allB);

    bool sentinelIntact = true;
    for (size_t i = 0; i < sizeof(guarded.sentinel); i++)
    {
      if (guarded.sentinel[i] != 'Z')
      {
        sentinelIntact = false;
      }
    }
    CHECK(sentinelIntact);
    CHECK_STR(guarded.level.name, "LEVEL");

    free(big);
  }
}

static void testForgiveness(void)
{
  TEST_GROUP("level parser: line endings, a missing name, odd characters");

  Level level;

  // CRLF is the same level as LF.
  CHECK(parse(&level, "# Name\r\nBBB\r\n_D_\r\n"));
  CHECK_STR(level.name, "Name");
  CHECK(rowIs(&level, 0, "BBB____________"));
  CHECK(rowIs(&level, 1, "_D_____________"));
  CHECK(emptyFrom(&level, 2));

  // A file with no '#' line at all is called LEVEL, so the HUD has something
  // to print.
  CHECK(parse(&level, "BBB\n"));
  CHECK_STR(level.name, "LEVEL");

  // And a '#' with nothing after it names nothing, so it is LEVEL as well.
  CHECK(parse(&level, "#\nBBB\n"));
  CHECK_STR(level.name, "LEVEL");

  // A completely empty buffer is a valid, empty level.
  memset(&level, 'Z', sizeof(level));
  CHECK(parseLevelText(&level, "", 0));
  CHECK_STR(level.name, "LEVEL");
  CHECK_INT(level.boss, 0);
  CHECK(emptyFrom(&level, 0));

  // Anything that is not a cell character is an empty cell - including the
  // space the README says means empty, and lower-case brick letters.
  CHECK(parse(&level, "B x?9b.BS \n"));
  CHECK(rowIs(&level, 0, "B______BS______"));

  // Every character that *is* a cell comes through unchanged.
  CHECK(parse(&level, "BDTSXGFEVWU\n"));
  CHECK(rowIs(&level, 0, "BDTSXGFEVWU____"));

  // The struct is fully reset by the parse: nothing from before survives.
  CHECK(parse(&level, "\n"));
  CHECK_INT(level.boss, 0);
  CHECK(emptyFrom(&level, 0));
}

static void testLongName(void)
{
  TEST_GROUP("level parser: a name longer than the struct can hold");

  Level level;

  // 40 characters against LEVEL_NAME_MAX of 24: kept to what fits, and
  // always NUL-terminated inside the array.
  const char *longName = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcd";
  char text[80];

  snprintf(text, sizeof(text), "# %s\nBBB\n", longName);
  CHECK(strlen(longName) > LEVEL_NAME_MAX);

  CHECK(parse(&level, text));
  CHECK_INT(strlen(level.name), LEVEL_NAME_MAX - 1);
  CHECK_INT(level.name[LEVEL_NAME_MAX - 1], '\0');
  CHECK(strncmp(level.name, longName, LEVEL_NAME_MAX - 1) == 0);

  // The overflow went nowhere: the row after it is still where it should be.
  CHECK(rowIs(&level, 0, "BBB____________"));

  // Exactly the longest name that fits is kept whole.
  char exact[LEVEL_NAME_MAX];
  memset(exact, 'Q', LEVEL_NAME_MAX - 1);
  exact[LEVEL_NAME_MAX - 1] = '\0';
  snprintf(text, sizeof(text), "#%s\n", exact);

  CHECK(parse(&level, text));
  CHECK_STR(level.name, exact);
}

void testLevelParser(void)
{
  testMinimal();
  testBossDirective();
  testBossLookalikes();
  testRowShapes();
  testForgiveness();
  testLongName();
}
