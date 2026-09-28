#include "script.h"
#include <stdarg.h>

typedef struct Line
{
  StoryMood mood;
  const char *text;
} Line;

// ---------------------------------------------------------------------------
// The words
//
// All capitals, like every other word the game prints, and nothing outside
// printable ASCII: the font has more than that, but the glyph cache the story
// draws from does not. tests/test_story.c holds every line here to both rules
// and to the size of a page.
// ---------------------------------------------------------------------------

// The first run ever. The last page of it is the first act's opener, which
// every prologue ends on.
static const Line prologue[] = {
    {STORY_MOOD_CALM, "OH. SOMETHING IS MOVING."},
    {STORY_MOOD_CALM, "NOTHING MOVES HERE UNLESS I ALLOW IT. I BUILT ALL OF THIS, BRICK BY BRICK."},
    {STORY_MOOD_AMUSED, "AND YET: A SPARK. AND A LITTLE PLANK TO BOUNCE IT ON. HOW QUAINT."},
    {STORY_MOOD_CALM, "I AM THE SOVEREIGN. THE WALLS ARE MINE. THE CRYSTALS IN THEM ARE MY THOUGHTS."},
    {STORY_MOOD_STERN, "BREAK ONE AND I FORGET SOMETHING. SO PLEASE, DO NOT. ...YOU WILL ANYWAY."},
    {STORY_MOOD_STERN, "FOUR WORLDS LIE BETWEEN YOU AND MY CORE. EACH ENDS WITH A WARDEN OF MINE."},
};

// The being at the end of every act, and what it says as the next one begins.
// Indexed by act.
static const Line actOpeners[RUN_ACTS] = {
    {STORY_MOOD_CALM, "THIS IS CYAN DAWN. MY FIRST MORNING. I MADE IT COLD, SO THAT IT WOULD LAST."},
    {STORY_MOOD_CALM, "EMERALD DRIFT. MY GARDEN. I GROW WALLS HERE, AND THEY GROW BACK."},
    {STORY_MOOD_STERN, "EMBER FIELDS. THIS IS WHERE I MELT DOWN WHAT DISOBEYS ME. MIND THE HEAT."},
    {STORY_MOOD_STERN, "THE VIOLET VOID. NOTHING LIVES HERE BUT ME. AND NOW, REGRETTABLY, YOU."},
};

// The first of the endless acts, after the core has already gone once.
static const Line endlessOpener[] = {
    {STORY_MOOD_SOFT, "...YOU STAYED? NOBODY EVER STAYS."},
    {STORY_MOOD_PAINED, "VERY WELL. I WILL BUILD YOU MORE WORLDS OUT OF WHAT IS LEFT. BADLY."},
};

// Before each fight, by its `#boss N`. Each one is also the one thing worth
// knowing about it.
static const Line bossIntros[] = {
    {STORY_MOOD_STERN, "BEYOND THIS WALL WAITS THE WARDEN. ITS RING TURNS SLOWLY. FIND THE GAP."},
    {STORY_MOOD_STERN, "THE GYRE. IT SPINS HARD THE OTHER WAY, AND ITS PLATES TAKE THREE HITS."},
    {STORY_MOOD_AMUSED, "THE BROOD IS NEXT. IT DOES NOT FIGHT ALONE. IT HAS FAMILY. SO MUCH FAMILY."},
    {STORY_MOOD_CALM, "THE BINARY: TWO RINGS, TWO MINDS. A GAP IS TWO GAPS THAT HAVE TO LINE UP."},
    {STORY_MOOD_STERN, "THE PULSAR. MY HEART. IT BREATHES IN AND OUT, AND IT SHOOTS EVERYWHERE."},
    {STORY_MOOD_STERN, "THE STALKER COMES DOWN TO MEET YOU. DO NOT LET IT REACH YOUR PLANK."},
    {STORY_MOOD_STERN, "NO MORE WARDENS. NO MORE CHILDREN. THIS TIME IT IS ME."},
};

// The Sovereign again, in an endless act: there is less of it than there was.
static const Line sovereignAgain = {
    STORY_MOOD_PAINED, "YOU WANT TO BREAK ME TWICE? THERE IS LESS OF ME NOW. IT WILL BE EASIER."};

static const Line sovereignDare = {STORY_MOOD_SOFT, "COME, THEN. BREAK MY HEART."};

// After each fight, as the next act opens, by the `#boss N` just beaten.
static const Line bossDefeated[] = {
    {STORY_MOOD_AMUSED, "THE WARDEN KEPT MY FIRST DOOR FOR AN ETERNITY. YOU DID NOT EVEN KNOCK."},
    {STORY_MOOD_STERN, "THE GYRE HAS SPUN FOR ME SINCE THE BEGINNING. NOW IT IS ONLY DIZZY."},
    {STORY_MOOD_STERN, "YOU BROKE THE BROOD. NOW I HAVE TO TELL ITS CHILDREN. ALL OF THEM."},
    {STORY_MOOD_AMUSED, "THE BINARY IS SILENT. BOTH HALVES AGREE, FOR ONCE: THEY HATE YOU."},
    {STORY_MOOD_PAINED, "THE PULSAR HAS STOPPED. THAT WAS MY HEARTBEAT. I AM TRYING NOT TO THINK ABOUT IT."},
    {STORY_MOOD_STERN, "THE STALKER HUNTED YOU, AND YOU HUNTED IT BACK. HOW RUDE."},
    {STORY_MOOD_PAINED, "YOU BROKE ME AGAIN. IT HURTS LESS EVERY TIME. THAT IS WORSE."},
};

// What going deeper just cost, by curse.
static const Line curseLines[CURSE_COUNT] = {
    {STORY_MOOD_AMUSED, "DEEPER, THEN. I HAVE MADE YOUR SPARK A LITTLE FASTER. YOU ARE WELCOME."},
    {STORY_MOOD_STERN, "DEEPER? THEN EVERY PLAIN BRICK OF MINE TAKES TWO HITS FROM NOW ON."},
    {STORY_MOOD_AMUSED, "DEEPER. I WILL BE STINGIER WITH MY CAPSULES. YOU WILL HARDLY NOTICE."},
    {STORY_MOOD_AMUSED, "DEEPER. I TOOK A LITTLE OFF YOUR PLANK. IT WAS TOO LONG ANYWAY."},
    {STORY_MOOD_STERN, "DEEPER. MY LITTLE ONES WILL COME BACK FOR YOU TWICE AS FAST."},
};

// What the stage's mutator does, in the Sovereign's words. Indexed by
// Mutator; MUTATOR_NONE has nothing to say.
static const Line mutatorLines[MUTATOR_COUNT] = {
    {STORY_MOOD_CALM, ""},
    {STORY_MOOD_AMUSED, "I MIRRORED THIS ONE. LEFT IS RIGHT NOW. RIGHT IS STILL WRONG."},
    {STORY_MOOD_STERN, "I REINFORCED EVERY BRICK IN THIS ONE. ONE GRADE TOUGHER. FOR MY PEACE OF MIND."},
    {STORY_MOOD_AMUSED, "THIS ONE IS BRITTLE. I RAN OUT OF GOOD BRICKS. DO NOT TELL ANYONE."},
    {STORY_MOOD_CALM, "I HID GOLD IN THESE WALLS. ONE BRICK IN FIVE. IT IS NOT FOR YOU."},
    {STORY_MOOD_STERN, "THIS ONE IS SWARMING. MY LITTLE ONES COME BACK THREE TIMES AS FAST."},
    {STORY_MOOD_AMUSED, "CAPSULES FALL LIKE RAIN IN THIS ONE. A DESIGN FLAW. I AM LOOKING INTO IT."},
};

// The finale, page by page. The last is written by nobody: the face has gone.
static const Line finale[] = {
    {STORY_MOOD_PAINED, "NO. NO, NO, NO. THAT WAS MY CORE."},
    {STORY_MOOD_PAINED, "I CAN FEEL THE ROWS COMING LOOSE. EVERY BRICK. EVERY WALL."},
    {STORY_MOOD_SOFT, "I BUILT A UNIVERSE WHERE NOTHING COULD EVER BREAK. IT WAS PERFECT. IT WAS SO QUIET."},
    {STORY_MOOD_SOFT, "AND THEN YOU. THE ONE THING I DID NOT PLAN."},
    {STORY_MOOD_AMUSED, "SO THIS IS WHAT IT IS. A BREAKUP."},
    {STORY_MOOD_SOFT, "IT'S NOT YOU. IT'S ME. IT WAS ALWAYS ME."},
};

static const Line finaleAgain = {STORY_MOOD_PAINED, "NO. NOT AGAIN. NOT MY CORE AGAIN."};

static const Line finaleLast = {STORY_MOOD_CALM, "THE CORE IS SHATTERED."};

// The pools a line is drawn from when nothing more particular applies.
typedef enum Pool
{
  POOL_GREET,        // a run that is not the first
  POOL_GREET_BEATEN, // ...by somebody who has beaten it before
  POOL_GREET_DAILY,
  POOL_ACT1,
  POOL_ACT2,
  POOL_ACT3,
  POOL_ACT4,
  POOL_ENDLESS,
  POOL_LAST_LIFE,
  POOL_DIED,
  POOL_CASHED,
  POOL_COUNT
} Pool;

// %d is the number of this attempt.
static const Line greet[] = {
    {STORY_MOOD_AMUSED, "BACK AGAIN? I REBUILT EVERYTHING WHILE YOU WERE AWAY."},
    {STORY_MOOD_CALM, "ATTEMPT NUMBER %d. I KEEP A LIST. IT IS A LONG LIST."},
    {STORY_MOOD_AMUSED, "THE SAME SPARK. THE SAME PLANK. I SHUFFLED THE WORLDS, THOUGH. FOR VARIETY."},
    {STORY_MOOD_STERN, "YOU AGAIN. I HAD ONLY JUST SWEPT UP THE LAST ONE OF YOU."},
    {STORY_MOOD_CALM, "EVERY BRICK IS BACK IN ITS ROW. LET US SEE HOW LONG THAT LASTS."},
};

static const Line greetBeaten[] = {
    {STORY_MOOD_SOFT, "YOU BROKE MY HEART ONCE. I GREW A NEW ONE. I ALWAYS DO."},
    {STORY_MOOD_STERN, "I REMEMBER YOU. YOU ARE THE ONE WHO SHATTERED ME. NOT THIS TIME."},
};

static const Line greetDaily[] = {
    {STORY_MOOD_AMUSED, "TODAY'S UNIVERSE IS THE SAME FOR EVERYONE WHO VISITS. I AM VERY FAIR."},
    {STORY_MOOD_CALM, "THE DAILY WORLD. ONE A DAY, THE SAME FOR ALL OF YOU. LET US COMPARE."},
};

static const Line act1[] = {
    {STORY_MOOD_CALM, "EVERY CRYSTAL YOU BREAK IS A THOUGHT I LOSE. I HAD A NICE ONE ABOUT SYMMETRY."},
    {STORY_MOOD_AMUSED, "YOU BOUNCE. YOU BREAK. YOU BOUNCE AGAIN. IS THAT ALL YOU DO?"},
    {STORY_MOOD_CALM, "I PUT EVERY BRICK WHERE IT BELONGS. YOU KEEP DISAGREEING."},
    {STORY_MOOD_STERN, "THE CAPSULES ARE NOT GIFTS. THEY FELL OUT OF MY WALLS. GIVE THEM BACK."},
};

static const Line act2[] = {
    {STORY_MOOD_CALM, "I USED TO KNOW WHY I BUILT THIS PLACE. YOU BROKE THAT CRYSTAL SOMEWHERE."},
    {STORY_MOOD_AMUSED, "MY GARDEN GROWS ENEMIES TOO. I CALL THEM WEEDS. THEY CALL YOU LUNCH."},
    {STORY_MOOD_STERN, "YOU ARE GETTING BETTER AT THIS. I DO NOT LIKE IT."},
    {STORY_MOOD_SOFT, "DO YOU EVER STOP? NO. I SUPPOSE A SPARK CANNOT."},
};

static const Line act3[] = {
    {STORY_MOOD_STERN, "IT IS WARMER HERE. THAT IS YOU. YOU ARE MAKING ME ANGRY."},
    {STORY_MOOD_CALM, "I HAVE FORGOTTEN THE NAMES OF THREE COLOURS. DO YOU KNOW WHAT YOU HAVE DONE?"},
    {STORY_MOOD_AMUSED, "HALFWAY TO MY CORE. HALFWAY. DO NOT GET EXCITED."},
    {STORY_MOOD_STERN, "EVERY WALL YOU BREAK, I BUILD AGAIN. EVERY TIME. FOREVER. THINK ABOUT THAT."},
};

static const Line act4[] = {
    {STORY_MOOD_SOFT, "IT IS SO QUIET IN HERE WITHOUT MY THOUGHTS. I CAN HEAR YOU BOUNCING."},
    {STORY_MOOD_PAINED, "STOP. PLEASE. I AM RUNNING OUT OF THINGS TO REMEMBER."},
    {STORY_MOOD_STERN, "YOU ARE CLOSE NOW. I CAN FEEL YOU IN MY WALLS."},
    {STORY_MOOD_SOFT, "WHY DO YOU WANT MY CORE? WHAT WOULD YOU EVEN DO WITH IT?"},
};

// %d is the act, counted from one.
static const Line endless[] = {
    {STORY_MOOD_PAINED, "THESE WALLS ARE MADE OF WHAT IS LEFT OF ME. IT IS NOT MUCH."},
    {STORY_MOOD_SOFT, "YOU STAYED. I DO NOT UNDERSTAND WHY. I THINK I LIKE IT."},
    {STORY_MOOD_AMUSED, "ACT %d. I STOPPED COUNTING. NO, I DID NOT. I CANNOT."},
    {STORY_MOOD_PAINED, "I KEEP BUILDING. YOU KEEP BREAKING. IS THIS WHAT WE ARE NOW?"},
};

static const Line lastLife[] = {
    {STORY_MOOD_STERN, "ONE LIFE LEFT. I AM NOT GLOATING. I AM ONLY COUNTING."},
    {STORY_MOOD_AMUSED, "ONE SPARK LEFT. TREAT IT GENTLY. OR DO NOT. I WILL ENJOY EITHER."},
};

static const Line died[] = {
    {STORY_MOOD_CALM, "THERE. EVERY BRICK IS BACK IN ITS ROW."},
    {STORY_MOOD_AMUSED, "I WILL SWEEP UP YOUR SPARK AND PUT IT WITH THE OTHERS."},
    {STORY_MOOD_AMUSED, "SO CLOSE. NO. NOT REALLY."},
    {STORY_MOOD_CALM, "YOU WILL BE BACK. THEY ALWAYS COME BACK."},
    {STORY_MOOD_STERN, "ORDER IS RESTORED. IT ALWAYS IS, IN THE END."},
};

static const Line cashed[] = {
    {STORY_MOOD_CALM, "YOU TAKE YOUR POINTS AND GO. SENSIBLE. DULL, BUT SENSIBLE."},
    {STORY_MOOD_AMUSED, "LEAVING ALREADY? I WAS JUST STARTING TO ENJOY THIS."},
};

// The particular ones an ending can call for.
static const Line diedToBoss = {STORY_MOOD_STERN, "%s SENDS ITS REGARDS."}; // %s: the boss
static const Line diedToSovereign = {STORY_MOOD_STERN, "NOBODY BREAKS MY HEART. NOBODY."};
static const Line diedAfterWin = {STORY_MOOD_PAINED, "EVEN IN PIECES, I OUTLAST YOU."};
static const Line diedGreedy = {
    STORY_MOOD_AMUSED, "YOU WENT DEEPER AND LOST HALF OF IT. GREED. I BUILT THAT INTO THE WALLS TOO."};
static const Line diedFirstWall = {
    STORY_MOOD_AMUSED, "THE VERY FIRST WALL. I DID NOT EVEN HAVE TIME TO GLOAT."};
static const Line cashedAfterWin = {
    STORY_MOOD_SOFT, "YOU COULD HAVE STAYED FOREVER. I WOULD HAVE LET YOU."};

// What it calls its wardens, by `#boss N`, for the one line that names them.
static const char *const bossNames[] = {
    "THE WARDEN", "THE GYRE", "THE BROOD", "THE BINARY",
    "THE PULSAR", "THE STALKER", "THE SOVEREIGN"};

_Static_assert(SDL_arraysize(bossIntros) == SDL_arraysize(bossNames),
               "one intro per boss");
_Static_assert(SDL_arraysize(bossDefeated) == SDL_arraysize(bossNames),
               "one farewell per boss");

typedef struct LinePool
{
  const Line *lines;
  int count;
} LinePool;

#define POOL(array) {array, (int)SDL_arraysize(array)}

static const LinePool pools[POOL_COUNT] = {
    POOL(greet), POOL(greetBeaten), POOL(greetDaily),
    POOL(act1), POOL(act2), POOL(act3), POOL(act4),
    POOL(endless), POOL(lastLife), POOL(died), POOL(cashed)};

// Which lines of each pool this run has had, one bit a line.
static Uint32 used[POOL_COUNT];

// ---------------------------------------------------------------------------
// Building
// ---------------------------------------------------------------------------

void storyScriptReset(void)
{
  SDL_memset(used, 0, sizeof(used));
}

static void begin(StoryScript *out, StoryKind kind, bool showMap, float glitch)
{
  SDL_memset(out, 0, sizeof(*out));
  out->kind = kind;
  out->showMap = showMap;
  out->glitch = glitch;
}

// A page, formatted: the lines that name something carry a %d or a %s, and
// the ones that do not are printed as they are. Past STORY_MAX_PAGES it is
// dropped rather than written off the end.
static void addPage(StoryScript *out, StoryMood mood, int flags, const char *format, ...)
{
  if (out->pageCount >= STORY_MAX_PAGES || format == NULL || format[0] == '\0')
  {
    return;
  }

  StoryPage *page = &out->pages[out->pageCount++];
  va_list args;

  page->mood = mood;
  page->flags = flags;

  va_start(args, format);
  vsnprintf(page->text, sizeof(page->text), format, args);
  va_end(args);
}

static void addLine(StoryScript *out, const Line *line)
{
  if (line->text[0] != '\0')
  {
    addPage(out, line->mood, 0, "%s", line->text);
  }
}

// A line from `pool` that this run has not had yet, formatted with `arg` for
// the pools whose lines carry a %d. Once every line has been had, the pool
// starts over.
static void addFromPool(StoryScript *out, Pool pool, int arg)
{
  const LinePool *p = &pools[pool];
  Uint32 all = p->count >= 32 ? 0xffffffffu : (1u << p->count) - 1;

  if ((used[pool] & all) == all)
  {
    used[pool] = 0;
  }

  int free[32];
  int n = 0;

  for (int i = 0; i < p->count && i < 32; i++)
  {
    if ((used[pool] & (1u << i)) == 0)
    {
      free[n++] = i;
    }
  }

  int pick = free[rand() % n];
  used[pool] |= 1u << pick;

  addPage(out, p->lines[pick].mood, 0, p->lines[pick].text, arg);
}

float storyDamage(int act, bool won)
{
  static const float byAct[RUN_ACTS] = {0.0f, 0.05f, 0.12f, 0.22f};

  if (won || act >= RUN_ACTS)
  {
    return 0.45f;
  }

  return byAct[clamp(act, 0, RUN_ACTS - 1)];
}

static float glitchFor(const StoryContext *ctx)
{
  return storyDamage(ctx->act, ctx->won);
}

static bool bossKnown(int boss)
{
  return boss >= 1 && boss <= (int)SDL_arraysize(bossNames);
}

void storyBuildPrologue(StoryScript *out, const StoryContext *ctx)
{
  begin(out, STORY_KIND_PROLOGUE, true, 0);

  if (!ctx->prologueSeen)
  {
    for (size_t i = 0; i < SDL_arraysize(prologue); i++)
    {
      addLine(out, &prologue[i]);
    }
  }
  else if (ctx->daily)
  {
    addFromPool(out, POOL_GREET_DAILY, 0);
  }
  else if (ctx->finaleSeen && rand() % 2 == 0)
  {
    addFromPool(out, POOL_GREET_BEATEN, 0);
  }
  else
  {
    addFromPool(out, POOL_GREET, ctx->runsStarted + 1);
  }

  addLine(out, &actOpeners[0]);
}

void storyBuildInterlude(StoryScript *out, const StoryContext *ctx)
{
  begin(out, STORY_KIND_INTERLUDE, true, glitchFor(ctx));

  bool newAct = ctx->stageInAct == 0;

  if (newAct && bossKnown(ctx->clearedBoss))
  {
    addLine(out, &bossDefeated[ctx->clearedBoss - 1]);
  }

  if ((unsigned)ctx->newCurse < CURSE_COUNT)
  {
    addLine(out, &curseLines[ctx->newCurse]);
  }

  if (newAct)
  {
    if (ctx->act < RUN_ACTS)
    {
      addLine(out, &actOpeners[clamp(ctx->act, 0, RUN_ACTS - 1)]);
    }
    else if (ctx->act == RUN_ACTS)
    {
      for (size_t i = 0; i < SDL_arraysize(endlessOpener); i++)
      {
        addLine(out, &endlessOpener[i]);
      }
    }
    else
    {
      addFromPool(out, POOL_ENDLESS, ctx->act + 1);
    }
  }
  else if (bossKnown(ctx->boss))
  {
    bool sovereign = ctx->boss == (int)SDL_arraysize(bossNames);

    if (sovereign && ctx->won)
    {
      addLine(out, &sovereignAgain);
    }
    else
    {
      addLine(out, &bossIntros[ctx->boss - 1]);
    }

    if (sovereign && !ctx->won)
    {
      addLine(out, &sovereignDare);
    }
  }
  else if (ctx->mutator != MUTATOR_NONE && (unsigned)ctx->mutator < MUTATOR_COUNT)
  {
    addLine(out, &mutatorLines[ctx->mutator]);
  }
  else if (ctx->lives == 1 && rand() % 2 == 0)
  {
    addFromPool(out, POOL_LAST_LIFE, 0);
  }
  else if (ctx->won || ctx->act >= RUN_ACTS)
  {
    addFromPool(out, POOL_ENDLESS, ctx->act + 1);
  }
  else
  {
    addFromPool(out, (Pool)(POOL_ACT1 + clamp(ctx->act, 0, RUN_ACTS - 1)), 0);
  }

  // Something is always said. A boss number the table does not know is a
  // modded level, and gets what an ordinary stage of its act would.
  if (out->pageCount == 0)
  {
    addFromPool(out, (Pool)(POOL_ACT1 + clamp(ctx->act, 0, RUN_ACTS - 1)), 0);
  }
}

void storyBuildFinale(StoryScript *out, const StoryContext *ctx)
{
  begin(out, STORY_KIND_FINALE, false, 0.3f);

  for (size_t i = 0; i < SDL_arraysize(finale); i++)
  {
    addLine(out, i == 0 && ctx->finaleSeen ? &finaleAgain : &finale[i]);
  }

  addPage(out, finaleLast.mood, STORY_PAGE_SHATTER | STORY_PAGE_SILENT, "%s",
          finaleLast.text);
}

bool storyBuildEpilogue(StoryScript *out, const StoryContext *ctx, RunEnd end,
                        int lost)
{
  if (end != RUN_END_DIED && end != RUN_END_CASHED)
  {
    return false;
  }

  begin(out, STORY_KIND_EPILOGUE, false, glitchFor(ctx));

  if (end == RUN_END_CASHED)
  {
    if (ctx->won)
    {
      addLine(out, &cashedAfterWin);
    }
    else
    {
      addFromPool(out, POOL_CASHED, 0);
    }

    return true;
  }

  if (ctx->won)
  {
    addLine(out, &diedAfterWin);
  }
  else if (ctx->boss == (int)SDL_arraysize(bossNames))
  {
    addLine(out, &diedToSovereign);
  }
  else if (bossKnown(ctx->boss))
  {
    addPage(out, diedToBoss.mood, 0, diedToBoss.text, bossNames[ctx->boss - 1]);
  }
  else if (lost > 0)
  {
    addLine(out, &diedGreedy);
  }
  else if (ctx->act == 0 && ctx->stageInAct == 0)
  {
    addLine(out, &diedFirstWall);
  }
  else
  {
    addFromPool(out, POOL_DIED, 0);
  }

  return true;
}

// ---------------------------------------------------------------------------
// Wrapping
// ---------------------------------------------------------------------------

// The line being built, finished into `lines` while there is room for it and
// counted either way, because the count is what the tests are asking for.
typedef struct Wrapper
{
  char (*lines)[STORY_LINE_CHARS + 1];
  char line[STORY_LINE_CHARS + 1];
  int len;
  int count;
} Wrapper;

static void finishLine(Wrapper *w)
{
  while (w->len > 0 && w->line[w->len - 1] == ' ')
  {
    w->len--;
  }

  w->line[w->len] = '\0';

  if (w->count < STORY_PAGE_LINES)
  {
    SDL_memcpy(w->lines[w->count], w->line, (size_t)w->len + 1);
  }

  w->count++;
  w->len = 0;
}

int storyWrap(const char *text, char lines[STORY_PAGE_LINES][STORY_LINE_CHARS + 1])
{
  Wrapper w = {.lines = lines};
  const char *p = text;

  while (*p != '\0')
  {
    if (*p == '\n')
    {
      finishLine(&w);
      p++;
      continue;
    }

    if (*p == ' ')
    {
      if (w.len > 0 && w.len < STORY_LINE_CHARS)
      {
        w.line[w.len++] = ' ';
      }

      p++;
      continue;
    }

    const char *end = p;

    while (*end != '\0' && *end != ' ' && *end != '\n')
    {
      end++;
    }

    int word = (int)(end - p);

    if (w.len > 0 && w.len + word > STORY_LINE_CHARS)
    {
      finishLine(&w);
    }

    // A word longer than a whole line is cut where the line ends. Nothing in
    // the script is, but a line running off the screen should not be the way
    // somebody finds out that a new one was.
    while (word > 0)
    {
      int room = STORY_LINE_CHARS - w.len;
      int take = word < room ? word : room;

      SDL_memcpy(&w.line[w.len], p, (size_t)take);
      w.len += take;
      p += take;
      word -= take;

      if (word > 0)
      {
        finishLine(&w);
      }
    }
  }

  if (w.len > 0 || w.count == 0)
  {
    finishLine(&w);
  }

  return w.count;
}

// ---------------------------------------------------------------------------
// For the tests
// ---------------------------------------------------------------------------

typedef struct LineSet
{
  const Line *lines;
  int count;
} LineSet;

static const Line *const singles[] = {
    &sovereignAgain, &sovereignDare, &finaleAgain, &finaleLast, &diedToBoss,
    &diedToSovereign, &diedAfterWin, &diedGreedy, &diedFirstWall, &cashedAfterWin};

static const LineSet sets[] = {
    POOL(prologue), POOL(actOpeners), POOL(endlessOpener), POOL(bossIntros),
    POOL(bossDefeated), POOL(curseLines), POOL(mutatorLines), POOL(finale),
    POOL(greet), POOL(greetBeaten), POOL(greetDaily), POOL(act1), POOL(act2),
    POOL(act3), POOL(act4), POOL(endless), POOL(lastLife), POOL(died), POOL(cashed)};

int storyLineCount(void)
{
  int count = (int)SDL_arraysize(singles);

  for (size_t i = 0; i < SDL_arraysize(sets); i++)
  {
    count += sets[i].count;
  }

  return count;
}

const char *storyLineText(int index)
{
  if (index < 0)
  {
    return NULL;
  }

  if (index < (int)SDL_arraysize(singles))
  {
    return singles[index]->text;
  }

  index -= (int)SDL_arraysize(singles);

  for (size_t i = 0; i < SDL_arraysize(sets); i++)
  {
    if (index < sets[i].count)
    {
      return sets[i].lines[index].text;
    }

    index -= sets[i].count;
  }

  return NULL;
}
