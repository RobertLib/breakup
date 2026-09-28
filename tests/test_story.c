// What the Sovereign says, checked for fitting on the screen.
//
// A page is at most STORY_PAGE_LINES lines of STORY_LINE_CHARS glyphs, and the
// writing is drawn from a cache of the printable ASCII glyphs and nothing else.
// A line that breaks either rule does not fail anywhere loudly - it runs off
// the bottom of its box, or a letter goes missing from it - so every line in
// the script is held to both here, and so is every page the builders can put
// together, for every act, stage, boss, mutator, curse and ending there is.

#include "test.h"

#include "../src/globals.h"
#include "../src/story/script.h"

static bool fits(const char *text)
{
  char lines[STORY_PAGE_LINES][STORY_LINE_CHARS + 1];

  return storyWrap(text, lines) <= STORY_PAGE_LINES;
}

static bool printable(const char *text)
{
  for (const char *p = text; *p != '\0'; p++)
  {
    if (*p < 32 || *p > 126 || (*p >= 'a' && *p <= 'z'))
    {
      return false;
    }
  }

  return true;
}

// Every page of a script: something on it, in the glyphs there are, fitting.
static void checkScript(const StoryScript *script)
{
  CHECK(script->pageCount >= 1);
  CHECK(script->pageCount <= STORY_MAX_PAGES);
  CHECK(script->glitch >= 0.0f && script->glitch <= 1.0f);

  for (int i = 0; i < script->pageCount; i++)
  {
    const StoryPage *page = &script->pages[i];

    CHECK(page->text[0] != '\0');
    CHECK(printable(page->text));
    CHECK((unsigned)page->mood < STORY_MOOD_COUNT);

    if (!fits(page->text))
    {
      printf("       does not fit: \"%s\"\n", page->text);
      CHECK(false);
    }
  }
}

static void testWrap(void)
{
  TEST_GROUP("story: wrapping");

  char lines[STORY_PAGE_LINES][STORY_LINE_CHARS + 1];

  CHECK_INT(storyWrap("OH. SOMETHING IS MOVING.", lines), 1);
  CHECK_STR(lines[0], "OH. SOMETHING IS MOVING.");

  // Broken at the last space that fits, with nothing left hanging off the end.
  CHECK_INT(storyWrap("NOTHING MOVES HERE UNLESS I ALLOW IT. I BUILT ALL OF THIS.", lines), 2);
  CHECK_STR(lines[0], "NOTHING MOVES HERE UNLESS I");
  CHECK_STR(lines[1], "ALLOW IT. I BUILT ALL OF THIS.");

  // A newline is a break wherever it is, and spaces either side of one go.
  CHECK_INT(storyWrap("ONE  \n  TWO", lines), 2);
  CHECK_STR(lines[0], "ONE");
  CHECK_STR(lines[1], "TWO");

  // A word longer than a line is cut where the line ends rather than left to
  // run off the screen.
  CHECK_INT(storyWrap("ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJ", lines), 2);
  CHECK_INT((long long)strlen(lines[0]), STORY_LINE_CHARS);
  CHECK_STR(lines[1], "EFGHIJ");

  // Nothing at all is still one (empty) line, not none.
  CHECK_INT(storyWrap("", lines), 1);
  CHECK_STR(lines[0], "");

  // More lines than a page holds are counted, which is what the other tests
  // here ask for, and only the first STORY_PAGE_LINES are written.
  CHECK_INT(storyWrap("A\nB\nC\nD\nE\nF", lines), 6);
  CHECK_STR(lines[3], "D");
}

static void testEveryLine(void)
{
  TEST_GROUP("story: every line in the script fits on a page");

  int count = storyLineCount();

  CHECK(count > 50);

  for (int i = 0; i < count; i++)
  {
    const char *text = storyLineText(i);
    char filled[STORY_PAGE_TEXT];

    CHECK(text != NULL);

    if (text == NULL || text[0] == '\0')
    {
      continue;
    }

    // The ones that name a number or a boss, at the longest they can be.
    if (strstr(text, "%d") != NULL)
    {
      snprintf(filled, sizeof(filled), text, 99999);
    }
    else if (strstr(text, "%s") != NULL)
    {
      snprintf(filled, sizeof(filled), text, "THE SOVEREIGN");
    }
    else
    {
      snprintf(filled, sizeof(filled), "%s", text);
    }

    CHECK(printable(filled));
    CHECK(strlen(filled) < STORY_PAGE_TEXT - 1);

    if (!fits(filled))
    {
      printf("       does not fit: \"%s\"\n", filled);
      CHECK(false);
    }
  }

  CHECK(storyLineText(count) == NULL);
  CHECK(storyLineText(-1) == NULL);
}

static StoryContext baseContext(void)
{
  StoryContext ctx = {0};

  ctx.mutator = MUTATOR_NONE;
  ctx.newCurse = CURSE_COUNT;
  ctx.lives = 3;
  ctx.prologueSeen = true;

  return ctx;
}

static void testPrologue(void)
{
  TEST_GROUP("story: the prologue");

  StoryScript script;
  StoryContext ctx = baseContext();

  // The first time is the whole of it, and it ends on the first act.
  ctx.prologueSeen = false;
  storyBuildPrologue(&script, &ctx);
  checkScript(&script);
  CHECK(script.pageCount >= 5);
  CHECK(script.showMap);
  CHECK(strstr(script.pages[script.pageCount - 1].text, "CYAN DAWN") != NULL);

  // After that it is a greeting and the same last page - whichever greeting,
  // for a daily, for somebody who has won, for the thousandth attempt.
  for (int variant = 0; variant < 40; variant++)
  {
    ctx = baseContext();
    ctx.daily = variant % 3 == 0;
    ctx.finaleSeen = variant % 2 == 0;
    ctx.runsStarted = variant * 997;

    storyBuildPrologue(&script, &ctx);
    checkScript(&script);
    CHECK_INT(script.pageCount, 2);
    CHECK(strstr(script.pages[1].text, "CYAN DAWN") != NULL);
  }
}

static void testInterludes(void)
{
  TEST_GROUP("story: every interlude, for every stage of every act");

  StoryScript script;

  for (int act = 0; act < RUN_ACTS + 3; act++)
  {
    for (int stage = 0; stage < RUN_STAGES_PER_ACT; stage++)
    {
      bool bossStage = stage == RUN_STAGES_PER_ACT - 1;

      // Boss numbers past the seven there are stand for a modded level.
      for (int boss = 0; boss <= 9; boss++)
      {
        if (!bossStage && boss > 0)
        {
          break;
        }

        for (int m = 0; m < MUTATOR_COUNT; m++)
        {
          for (int c = 0; c <= CURSE_COUNT; c++)
          {
            StoryContext ctx = baseContext();

            ctx.act = act;
            ctx.stageInAct = stage;
            ctx.boss = boss;
            ctx.clearedBoss = stage == 0 && act > 0 ? (act * 2 + m) % 10 : 0;
            ctx.mutator = (Mutator)m;
            ctx.newCurse = (Curse)c;
            ctx.lives = 1 + (m + c) % 3;
            ctx.won = act >= RUN_ACTS;

            storyBuildInterlude(&script, &ctx);
            checkScript(&script);
            CHECK(script.showMap);
          }
        }
      }
    }
  }

  // The most an interlude says is a farewell, a curse and two openers: four
  // pages, short enough to sit through between two levels.
  StoryContext busiest = baseContext();
  busiest.act = RUN_ACTS;
  busiest.clearedBoss = 7;
  busiest.newCurse = CURSE_NARROW;
  busiest.won = true;

  storyBuildInterlude(&script, &busiest);
  CHECK(script.pageCount <= 4);
}

static void testNoRepeats(void)
{
  TEST_GROUP("story: a run hears every aside before it hears one twice");

  StoryScript script;
  StoryContext ctx = baseContext();
  char seen[4][STORY_PAGE_TEXT];

  ctx.stageInAct = 1;
  storyScriptReset();

  for (int i = 0; i < 4; i++)
  {
    storyBuildInterlude(&script, &ctx);
    CHECK_INT(script.pageCount, 1);
    snprintf(seen[i], sizeof(seen[i]), "%s", script.pages[0].text);

    for (int j = 0; j < i; j++)
    {
      CHECK(strcmp(seen[i], seen[j]) != 0);
    }
  }

  // And then the pool starts over rather than running dry.
  storyBuildInterlude(&script, &ctx);
  CHECK_INT(script.pageCount, 1);
}

static void testFinale(void)
{
  TEST_GROUP("story: the finale ends with the face gone");

  StoryScript script;
  StoryContext ctx = baseContext();

  for (int seen = 0; seen < 2; seen++)
  {
    ctx.finaleSeen = seen != 0;
    storyBuildFinale(&script, &ctx);
    checkScript(&script);
    CHECK(!script.showMap);

    const StoryPage *last = &script.pages[script.pageCount - 1];

    CHECK(last->flags & STORY_PAGE_SHATTER);
    CHECK(last->flags & STORY_PAGE_SILENT);

    // Only the last page breaks it: a face that has already gone cannot say
    // the pages before.
    for (int i = 0; i < script.pageCount - 1; i++)
    {
      CHECK(script.pages[i].flags == 0);
    }
  }
}

static void testEpilogues(void)
{
  TEST_GROUP("story: an ending is spoken over when it was died or cashed");

  StoryScript script;
  const RunEnd ends[] = {RUN_END_DIED, RUN_END_QUIT, RUN_END_CASHED, RUN_END_WON};

  for (size_t e = 0; e < sizeof(ends) / sizeof(ends[0]); e++)
  {
    for (int act = 0; act < RUN_ACTS + 2; act++)
    {
      for (int boss = 0; boss <= 9; boss++)
      {
        StoryContext ctx = baseContext();

        ctx.act = act;
        ctx.stageInAct = boss > 0 ? RUN_STAGES_PER_ACT - 1 : act % RUN_STAGES_PER_ACT;
        ctx.boss = boss;
        ctx.won = act >= RUN_ACTS;

        bool said = storyBuildEpilogue(&script, &ctx, ends[e], boss % 2 ? 500 : 0);
        bool shouldSay = ends[e] == RUN_END_DIED || ends[e] == RUN_END_CASHED;

        CHECK(said == shouldSay);

        if (said)
        {
          checkScript(&script);
          CHECK_INT(script.pageCount, 1);
          CHECK(!script.showMap);
        }
      }
    }
  }
}

void testStory(void)
{
  testWrap();
  testEveryLine();
  testPrologue();
  testInterludes();
  testNoRepeats();
  testFinale();
  testEpilogues();

  storyScriptReset();
}
