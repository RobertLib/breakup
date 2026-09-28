#include "story.h"
#include "being.h"
#include "pen.h"
#include "script.h"
#include "../level-manager.h"
#include "../lib/audio.h"
#include "../lib/gfx.h"
#include "../lib/save.h"
#include "../lib/tunnel.h"
#include "../lib/vector.h"
#include "../paddle/paddle.h"
#include "../types.h"

// The writing is drawn a glyph at a time, so that each letter can burn in on
// its own. The font is monospaced, which is what makes that a table of
// textures rather than a layout engine.
#define GLYPH_FIRST 32
#define GLYPH_LAST 126
#define GLYPH_COUNT (GLYPH_LAST - GLYPH_FIRST + 1)
#define PAGE_GLYPHS (STORY_PAGE_LINES * STORY_LINE_CHARS)

// Where things go. The face drifts about BEING_Y; the writing is a box of
// STORY_PAGE_LINES lines under it, with a page's lines centred in the box; the
// map of the run sits under that when the scene has one.
#define BEING_Y 168.0f
#define TEXT_TOP_MAP 316.0f
#define TEXT_TOP_BARE 340.0f
#define LINE_STEP 34.0f
#define MAP_TITLE_Y 462.0f
#define MAP_DETAIL_Y 484.0f
#define MAP_NODES_Y 540.0f

// How long things take, in seconds of real time.
#define FADE_TIME 0.45f   // the scene coming up over a level
#define ENTER_TIME 0.95f  // the face arriving, before its first letter
#define ERASE_TIME 0.45f  // a page dissolving
#define EXIT_TIME 0.8f    // the face going, and the scene with it
#define INPUT_DELAY 0.35f // so that the key which took a perk does not also skip


typedef enum Phase
{
  PHASE_ENTER,
  PHASE_TYPING,
  PHASE_HOLD,
  PHASE_ERASE,
  PHASE_EXIT
} Phase;

typedef struct Glyph
{
  char code;
  int line, col;
  float born; // scene time it was written at, or < 0 for not yet
} Glyph;

static SDL_Texture *glyphTextures[GLYPH_COUNT];
static float glyphW = 24.0f;
static float glyphH = 24.0f;

static bool active;
static StoryScript script;
static StoryContext context;
static Phase phase;
static float phaseT;
static float sceneT;
static float alpha; // the whole scene, over whatever is under it
static bool fadeIn;
static void (*onDone)(void);

// The page being written.
static int page;
static Glyph glyphs[PAGE_GLYPHS];
static int glyphCount;
static int lineLength[STORY_PAGE_LINES];
static int lineCount;
static int typed;
static float typeWait;
static float holdTime;

// The voice, as it goes along the page.
static int syllableGap; // letters to go before the next syllable
static unsigned wordHash;
static int syllableInWord;
static bool wordRising; // the word ends in a question mark

// The face, the stylus it writes with, and the tunnel behind them.
static Being being;
static Pen pen;
static Tunnel tunnel;
static float anchorX;

static SDL_Color tint;
static SDL_Color backdrop;
static float flash;

static SDL_Texture *mapTitle;
static SDL_Texture *mapDetail;
static SDL_Texture *hintText;


static bool prologueRequested;
static bool epilogueRequested;
static const char *devScene;

// Which curses the run had at the last interlude, so the next can tell which
// one going deeper has just added.
static bool curseSeen[CURSE_COUNT];

// How the run the epilogue is about ended, for its colours.
static RunEnd lastEnd;

static const SDL_Color white = {255, 255, 255, 255};

// ---------------------------------------------------------------------------
// Setting up
// ---------------------------------------------------------------------------

static bool storyEnabled(void)
{
  return saveData.story;
}

static void destroyTexture(SDL_Texture **texture)
{
  if (*texture != NULL)
  {
    SDL_DestroyTexture(*texture);
    *texture = NULL;
  }
}

static void destroyTexts(void)
{
  destroyTexture(&mapTitle);
  destroyTexture(&mapDetail);
  destroyTexture(&hintText);
}

static void ensureGlyphs(void)
{
  if (glyphTextures['M' - GLYPH_FIRST] != NULL)
  {
    return;
  }

  for (int i = 0; i < GLYPH_COUNT; i++)
  {
    char text[2] = {(char)(GLYPH_FIRST + i), '\0'};
    glyphTextures[i] = renderTextBlended(font24, text, white);
  }

  if (glyphTextures['M' - GLYPH_FIRST] != NULL)
  {
    SDL_FPoint size = getSize(glyphTextures['M' - GLYPH_FIRST]);
    glyphW = size.x;
    glyphH = size.y;
  }
}

static void buildContext(StoryContext *ctx, int cleared)
{
  SDL_zerop(ctx);

  ctx->act = runAct();
  ctx->stageInAct = runStageInAct();
  ctx->boss = runStageIsBoss() ? getLevel(runLevel())->boss : 0;
  ctx->clearedBoss = cleared >= 0 && isBossLevel(cleared) ? getLevel(cleared)->boss : 0;
  ctx->mutator = runMutator();
  ctx->newCurse = CURSE_COUNT;

  for (int i = 0; i < CURSE_COUNT; i++)
  {
    bool has = runHasCurse((Curse)i);

    if (has && !curseSeen[i])
    {
      ctx->newCurse = (Curse)i;
    }

    curseSeen[i] = has;
  }

  ctx->lives = paddle.lives;
  ctx->won = runWon();
  ctx->daily = runIsDaily();
  ctx->runsStarted = saveData.runsStarted;
  ctx->prologueSeen = (saveData.storySeen & STORY_SEEN_PROLOGUE) != 0;
  ctx->finaleSeen = (saveData.storySeen & STORY_SEEN_FINALE) != 0;
}

static SDL_Color darken(SDL_Color c, float by)
{
  return (SDL_Color){(Uint8)(c.r * by), (Uint8)(c.g * by), (Uint8)(c.b * by), 255};
}

// The colours of the stage the scene is about: the next world's for a
// prologue or an interlude, the void's for the finale, and the world the run
// ended in, bled towards red or gold, for the epilogue.
// The world a scene belongs to. A run's act is a world, whichever file the
// stage drew - the second boss of the first act lives in the second world's
// stretch of the level files, and is still fought in Cyan Dawn - and only the
// endless acts, which draw from everywhere, are the level's own.
static int sceneWorld(void)
{
  if (context.act >= 0 && context.act < RUN_ACTS && context.act < WORLD_COUNT)
  {
    return context.act;
  }

  return clamp(worldForLevel(paddle.level), 0, WORLD_COUNT - 1);
}

static void chooseColors(void)
{
  int world = sceneWorld();

  tint = worldThemes[world].glow;
  backdrop = darken(worldThemes[world].deep, 0.7f);

  switch (script.kind)
  {
  case STORY_KIND_FINALE:
    tint = worldThemes[WORLD_COUNT - 1].glow;
    backdrop = darken(worldThemes[WORLD_COUNT - 1].deep, 0.6f);
    break;

  // A run that died is spoken over in red, one that was cashed out in gold.
  case STORY_KIND_EPILOGUE:
    if (script.pageCount > 0 && lastEnd == RUN_END_CASHED)
    {
      tint = mixColor(tint, (SDL_Color){255, 210, 110, 255}, 0.7f);
      backdrop = mixColor(backdrop, (SDL_Color){30, 22, 6, 255}, 0.5f);
    }
    else
    {
      tint = mixColor(tint, (SDL_Color){255, 80, 80, 255}, 0.8f);
      backdrop = mixColor(backdrop, (SDL_Color){34, 6, 10, 255}, 0.6f);
    }
    break;

  default:
    break;
  }

  // Put back together out of what was left: paler, and warmer.
  if (context.won && script.kind != STORY_KIND_FINALE)
  {
    tint = mixColor(tint, (SDL_Color){255, 230, 190, 255}, 0.35f);
  }
}

static void uppercase(char *out, size_t size, const char *in)
{
  size_t i = 0;

  for (; in[i] != '\0' && i + 1 < size; i++)
  {
    out[i] = (char)SDL_toupper((unsigned char)in[i]);
  }

  out[i] = '\0';
}

// What the map says about the stage coming up: where it is and what it is
// called, and underneath what the run is doing to it.
static void buildMapText(void)
{
  if (!script.showMap)
  {
    return;
  }

  char name[64];
  char buf[160];
  int world = sceneWorld();
  const char *label = context.daily ? "DAILY" : "ACT";

  uppercase(name, sizeof(name), getLevel(paddle.level)->name);

  if (context.boss > 0)
  {
    snprintf(buf, sizeof(buf), "%s  -  %s %d BOSS  -  %s", worldThemes[world].name,
             label, context.act + 1, name);
  }
  else
  {
    snprintf(buf, sizeof(buf), "%s  -  %s %d-%d  -  %s", worldThemes[world].name,
             label, context.act + 1, context.stageInAct + 1, name);
  }

  mapTitle = renderTextBlended(font16, buf, mixColor(tint, white, 0.3f));

  // Underneath, what the run is doing to it: the stage's mutator, and the
  // price of having gone deeper. A line each, in the orange the level banner
  // uses for the same two things.
  Mutator mutator = runMutator();
  int curses = runCurseCount();
  char mutatorLine[96] = "";
  char curseLine[64] = "";

  if (mutator != MUTATOR_NONE)
  {
    snprintf(mutatorLine, sizeof(mutatorLine), "%s - %s", mutatorName(mutator),
             mutatorDescription(mutator));
  }

  if (curses > 0)
  {
    snprintf(curseLine, sizeof(curseLine), "SCORE X%.1f  -  %d CURSE%s",
             runScoreMultiplier(), curses, curses == 1 ? "" : "S");
  }

  snprintf(buf, sizeof(buf), "%s%s%s", mutatorLine,
           mutatorLine[0] != '\0' && curseLine[0] != '\0' ? "\n" : "", curseLine);

  if (buf[0] != '\0')
  {
    mapDetail = renderTextWrapped(font16, buf, (SDL_Color){255, 170, 90, 255},
                                  SCREEN_WIDTH - 40, true);
  }
}

static void finish(void);
static void startPage(int index);

static void begin(bool fade, void (*done)(void))
{
  if (script.pageCount == 0)
  {
    if (done != NULL)
    {
      done();
    }

    return;
  }

  ensureGlyphs();
  destroyTexts();

  active = true;
  fadeIn = fade;
  alpha = fade ? 0.0f : 1.0f;
  onDone = done;

  phase = PHASE_ENTER;
  phaseT = 0;
  sceneT = 0;
  page = 0;
  glyphCount = 0;
  lineCount = 0;
  typed = 0;
  flash = 0;

  chooseColors();

  beingReset(&being);
  beingSetGlitch(&being, script.glitch);
  beingSetMood(&being, script.pages[0].mood);
  beingSetTarget(&being, SCREEN_WIDTH / 2.0f, BEING_Y);

  anchorX = SCREEN_WIDTH / 2.0f;
  penReset(&pen, SCREEN_WIDTH / 2.0f, BEING_Y + 60.0f);
  tunnelReset(&tunnel);

  buildMapText();
  hintText = renderTextBlended(font16, "SPACE - NEXT      ESC - SKIP",
                               (SDL_Color){110, 120, 160, 255});

  playSfx(SFX_STORY_APPEAR);
}

// ---------------------------------------------------------------------------
// The hosts' calls
// ---------------------------------------------------------------------------

void storyRequestPrologue(void)
{
  prologueRequested = true;
}

void storyRequestEpilogue(void)
{
  epilogueRequested = true;
}

void storyDevRequest(const char *scene)
{
  if (SDL_strcmp(scene, "prologue") == 0)
  {
    prologueRequested = true;
  }
  else if (SDL_strcmp(scene, "epilogue") == 0)
  {
    epilogueRequested = true;
  }
  else if (SDL_strcmp(scene, "interlude") == 0)
  {
    devScene = "interlude";
  }
  else if (SDL_strcmp(scene, "finale") == 0)
  {
    devScene = "finale";
  }
  else
  {
    fprintf(stderr, "BREAKUP_STORY=%s is not a scene; ignored\n", scene);
  }
}

void storyRunBegan(void)
{
  storyStop();
  storyScriptReset();

  for (int i = 0; i < CURSE_COUNT; i++)
  {
    curseSeen[i] = runHasCurse((Curse)i);
  }

  bool prologue = prologueRequested;
  const char *dev = devScene;

  prologueRequested = false;
  devScene = NULL;

  if (prologue)
  {
    if (storyEnabled())
    {
      buildContext(&context, -1);
      storyBuildPrologue(&script, &context);

      // No fade: the menu's own fade to black is what this comes up out of.
      begin(false, NULL);

      saveData.storySeen |= STORY_SEEN_PROLOGUE;
    }

    // Counted whether or not it is said out loud.
    saveData.runsStarted++;
    writeSave();
  }
  else if (dev != NULL && SDL_strcmp(dev, "interlude") == 0)
  {
    storyPlayInterlude(-1);
  }
  else if (dev != NULL && SDL_strcmp(dev, "finale") == 0)
  {
    storyPlayFinale(NULL);
  }
}

void storyPlayInterlude(int cleared)
{
  if (!storyEnabled() || !runActive())
  {
    return;
  }

  buildContext(&context, cleared);
  storyBuildInterlude(&script, &context);
  begin(true, NULL);
}

void storyPlayFinale(void (*done)(void))
{
  if (!storyEnabled())
  {
    if (done != NULL)
    {
      done();
    }

    return;
  }

  buildContext(&context, -1);
  storyBuildFinale(&script, &context);

  // The boss theme has nothing left to say; the hum is all there is under this.
  stopMusic();
  begin(true, done);

  saveData.storySeen |= STORY_SEEN_FINALE;
  writeSave();
}

bool storyPlayEpilogue(const RunResult *result, void (*done)(void))
{
  bool requested = epilogueRequested;
  epilogueRequested = false;

  if (!requested || !storyEnabled() || result == NULL)
  {
    return false;
  }

  SDL_zero(context);
  context.act = result->act - 1;
  context.stageInAct = result->stageInAct - 1;
  context.boss = isBossLevel(paddle.level) ? getLevel(paddle.level)->boss : 0;
  context.won = result->won;
  context.daily = result->daily;
  context.newCurse = CURSE_COUNT;
  lastEnd = result->end;

  if (!storyBuildEpilogue(&script, &context, result->end, result->lost))
  {
    return false;
  }

  // Opaque from the first frame: the screen under it is the result, which the
  // Sovereign gets to say its piece before.
  begin(false, done);

  return active;
}

bool storyActive(void)
{
  return active;
}

bool storyCoversScreen(void)
{
  return active && alpha >= 0.999f;
}

void storyStop(void)
{
  if (!active)
  {
    return;
  }

  active = false;
  onDone = NULL;
  destroyTexts();
  setMusicDuck(1.0f);
  setDroneGain(0);
}

void destroyStory(void)
{
  storyStop();

  for (int i = 0; i < GLYPH_COUNT; i++)
  {
    destroyTexture(&glyphTextures[i]);
  }
}

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

static bool isWordChar(char c)
{
  return SDL_isalpha((unsigned char)c) || c == '\'';
}

static float textTop(void)
{
  float top = script.showMap ? TEXT_TOP_MAP : TEXT_TOP_BARE;

  return top + (STORY_PAGE_LINES - lineCount) * LINE_STEP * 0.5f;
}

static SDL_FPoint glyphCentre(int index)
{
  const Glyph *g = &glyphs[clamp(index, 0, SDL_max(glyphCount - 1, 0))];
  float x = SCREEN_WIDTH / 2.0f - lineLength[g->line] * glyphW / 2.0f + g->col * glyphW;
  float y = textTop() + g->line * LINE_STEP;

  return (SDL_FPoint){x + glyphW / 2.0f, y + glyphH / 2.0f};
}

static const StoryPage *currentPage(void)
{
  return &script.pages[clamp(page, 0, script.pageCount - 1)];
}

static void startPage(int index)
{
  char lines[STORY_PAGE_LINES][STORY_LINE_CHARS + 1];

  page = index;

  const StoryPage *p = currentPage();

  lineCount = SDL_min(storyWrap(p->text, lines), STORY_PAGE_LINES);
  glyphCount = 0;

  for (int l = 0; l < lineCount; l++)
  {
    int len = (int)SDL_strlen(lines[l]);

    lineLength[l] = len;

    for (int c = 0; c < len && glyphCount < PAGE_GLYPHS; c++)
    {
      glyphs[glyphCount++] = (Glyph){lines[l][c], l, c, -1.0f};
    }
  }

  typed = 0;
  typeWait = 0.12f;
  holdTime = clamp(1.0f + glyphCount * 0.028f, 1.6f, 4.0f);
  syllableGap = 0;

  beingSetMood(&being, p->mood);

  // Pain shows: the face holds together worse while it hurts.
  beingSetGlitch(&being, script.glitch + (p->mood == STORY_MOOD_PAINED ? 0.15f : 0.0f));

  if (p->flags & STORY_PAGE_SHATTER)
  {
    beingShatter(&being);
    flash = 1.0f;
    tint = (SDL_Color){255, 226, 170, 255};
    playSfx(SFX_STORY_SHATTER);
    addHitstop(0.12f);
  }

  anchorX = SCREEN_WIDTH / 2.0f + frandRange(-110.0f, 110.0f);

  phase = PHASE_TYPING;
  phaseT = 0;
}

static float letterTime(char c)
{
  static const float perMood[STORY_MOOD_COUNT] = {
      [STORY_MOOD_CALM] = 1.0f / 26.0f,
      [STORY_MOOD_AMUSED] = 1.0f / 30.0f,
      [STORY_MOOD_STERN] = 1.0f / 24.0f,
      [STORY_MOOD_PAINED] = 1.0f / 18.0f,
      [STORY_MOOD_SOFT] = 1.0f / 19.0f,
  };

  const StoryPage *p = currentPage();
  float t = p->flags & STORY_PAGE_SILENT ? 1.0f / 12.0f : perMood[p->mood];

  if (c == '.' || c == '!' || c == '?')
  {
    t += 0.28f;
  }
  else if (c == ',' || c == ':' || c == ';')
  {
    t += 0.12f;
  }

  // A voice in pain catches.
  if (p->mood == STORY_MOOD_PAINED && frand() < 0.08f)
  {
    t += 0.15f;
  }

  return t;
}

static int vowelOf(char c)
{
  switch (SDL_toupper((unsigned char)c))
  {
  case 'A':
    return 0;
  case 'E':
    return 1;
  case 'I':
  case 'Y':
    return 2;
  case 'O':
    return 3;
  case 'U':
    return 4;
  default:
    return -1;
  }
}

// One letter's worth of the voice. A syllable is spoken at the start of every
// word and then every second or third letter of it, sung on the next vowel
// the word has coming, at a pitch that comes from the word itself - so that a
// word the Sovereign says twice, it says the same way twice.
static void speakLetter(int index)
{
  const Glyph *g = &glyphs[index];

  if (!SDL_isalpha((unsigned char)g->code))
  {
    return;
  }

  bool wordStart = g->col == 0 || !isWordChar(glyphs[index - 1].code);

  if (wordStart)
  {
    int end = index;

    wordHash = 2166136261u;

    while (end < glyphCount && glyphs[end].line == g->line && isWordChar(glyphs[end].code))
    {
      wordHash = (wordHash ^ (unsigned char)glyphs[end].code) * 16777619u;
      end++;
    }

    wordRising = end < glyphCount && glyphs[end].line == g->line && glyphs[end].code == '?';
    syllableInWord = 0;
    syllableGap = 0;
  }

  if (syllableGap > 0)
  {
    syllableGap--;
    return;
  }

  int vowel = -1;

  for (int i = index; i < glyphCount && glyphs[i].line == g->line && isWordChar(glyphs[i].code); i++)
  {
    if ((vowel = vowelOf(glyphs[i].code)) >= 0)
    {
      break;
    }
  }

  if (vowel < 0)
  {
    vowel = (int)(wordHash % VOICE_VOWELS);
  }

  static const float moodPitch[STORY_MOOD_COUNT] = {
      [STORY_MOOD_CALM] = 0.0f,
      [STORY_MOOD_AMUSED] = 2.0f,
      [STORY_MOOD_STERN] = -3.0f,
      [STORY_MOOD_PAINED] = -4.0f,
      [STORY_MOOD_SOFT] = -1.0f,
  };

  // A minor pentatonic either side of the voice's own note: whatever the
  // words, it sounds like a tune in one key rather than like noise.
  static const float scale[5] = {-5.0f, -2.0f, 0.0f, 2.0f, 5.0f};

  float pitch = moodPitch[currentPage()->mood] +
                scale[(wordHash + (unsigned)syllableInWord * 3u) % 5u] +
                frandRange(-0.3f, 0.3f);

  if (wordRising)
  {
    pitch += 3.0f + syllableInWord;
  }

  playVoice(vowel, pitch, 0.85f);
  beingSpeak(&being, 0.6f + 0.4f * frand());

  syllableInWord++;
  syllableGap = 1 + (int)((wordHash + (unsigned)syllableInWord) % 2u);
}

static void typeLetters(float dt)
{
  bool silent = (currentPage()->flags & STORY_PAGE_SILENT) != 0;

  typeWait -= dt;

  while (typeWait <= 0 && typed < glyphCount)
  {
    Glyph *g = &glyphs[typed];

    // Back-dated by however far into this frame it fell due, so that letters
    // written in the same frame still burn in one after another.
    g->born = sceneT + typeWait;

    if (!silent)
    {
      speakLetter(typed);
    }

    if (g->code != ' ')
    {
      SDL_FPoint at = glyphCentre(typed);

      penSpark(&pen, at.x, at.y, 90.0f);
      penSpark(&pen, at.x, at.y, 60.0f);
    }

    typeWait += letterTime(g->code);
    typed++;
  }
}

static void writeTheRest(void)
{
  for (int i = typed; i < glyphCount; i++)
  {
    glyphs[i].born = sceneT - 0.3f + (i - typed) * 0.004f;
  }

  typed = glyphCount;
}

static void beginExit(void)
{
  if (phase == PHASE_EXIT)
  {
    return;
  }

  phase = PHASE_EXIT;
  phaseT = 0;
  beingSetVisible(&being, false);
}

static void finish(void)
{
  void (*done)(void) = onDone;

  storyStop();

  if (done != NULL)
  {
    done();
  }
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------

void updateStory(void)
{
  if (!active)
  {
    return;
  }

  float dt = (float)realDt;

  sceneT += dt;
  phaseT += dt;
  flash = fmaxf(0, flash - dt * 1.4f);

  bool ready = sceneT > INPUT_DELAY;
  bool advance = ready && (isKeyPressed[K_SPACE] || isKeyPressed[K_RETURN] || isMousePressed[1]);

  if (ready && isKeyPressed[K_ESCAPE])
  {
    beginExit();
    advance = false;
  }

  switch (phase)
  {
  case PHASE_ENTER:
    alpha = fadeIn ? clamp(phaseT / FADE_TIME, 0.0f, 1.0f) : 1.0f;

    if (phaseT >= ENTER_TIME || advance)
    {
      alpha = 1.0f;
      startPage(0);
    }
    break;

  case PHASE_TYPING:
    typeLetters(dt);

    if (advance)
    {
      writeTheRest();
    }

    if (typed >= glyphCount)
    {
      phase = PHASE_HOLD;
      phaseT = 0;
    }
    break;

  case PHASE_HOLD:
    if ((advance && phaseT > 0.1f) || phaseT >= holdTime)
    {
      if (page >= script.pageCount - 1)
      {
        beginExit();
      }
      else
      {
        phase = PHASE_ERASE;
        phaseT = 0;
        playSfx(SFX_STORY_PAGE);

        for (int i = 0; i < glyphCount; i += 3)
        {
          if (glyphs[i].code != ' ')
          {
            SDL_FPoint at = glyphCentre(i);
            penSpark(&pen, at.x, at.y, 50.0f);
          }
        }
      }
    }
    break;

  case PHASE_ERASE:
    if (phaseT >= ERASE_TIME || advance)
    {
      startPage(page + 1);
    }
    break;

  case PHASE_EXIT:
    alpha = clamp(1.0f - phaseT / EXIT_TIME, 0.0f, 1.0f);
    break;
  }

  // The stylus follows the letter being written, and while nothing is being
  // written it goes back to hover by the face.
  bool writing = phase == PHASE_TYPING && glyphCount > 0;
  SDL_FPoint want = {being.x + 120.0f, being.y + 40.0f};

  if (writing)
  {
    // Just above and to the right of the letter going down, like the tip of
    // a pen, rather than on top of the letter it is supposed to be writing.
    want = glyphCentre(typed);
    want.x += glyphW * 0.6f;
    want.y -= glyphH * 0.9f;
  }

  penFollow(&pen, want.x, want.y, writing, dt);
  updatePen(&pen, dt);

  // The face leans after the writing, and looks at it; between pages it
  // looks out at whoever is reading.
  beingSetTarget(&being, lerp(anchorX, pen.x, writing ? 0.35f : 0.0f), BEING_Y);

  if (writing)
  {
    beingLookAt(&being, pen.x, pen.y);
  }
  else
  {
    beingLookAt(&being, being.x, being.y + 240.0f);
  }

  updateBeing(&being, dt);
  updateTunnel(&tunnel, dt);

  // The music ducks under the voice and the hum comes up under both, in step
  // with the scene coming and going.
  float presence = alpha;

  setMusicDuck(1.0f - 0.72f * presence);
  setDroneGain(0.6f * presence);

  if (phase == PHASE_EXIT && phaseT >= EXIT_TIME)
  {
    finish();
  }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

static void drawGlowAt(float x, float y, float size, SDL_Color color, float a)
{
  if (a <= 0.004f)
  {
    return;
  }

  SDL_SetTextureColorMod(texGlow, color.r, color.g, color.b);
  SDL_SetTextureAlphaMod(texGlow, (Uint8)(255 * clamp(a, 0.0f, 1.0f)));
  SDL_FRect dst = {x - size / 2, y - size / 2, size, size};
  SDL_RenderTexture(renderer, texGlow, NULL, &dst);
  SDL_SetTextureColorMod(texGlow, 255, 255, 255);
  SDL_SetTextureAlphaMod(texGlow, 255);
}

static void drawText(float a)
{
  if (phase == PHASE_ENTER)
  {
    return;
  }

  bool silent = (currentPage()->flags & STORY_PAGE_SILENT) != 0;
  SDL_Color settled = silent ? (SDL_Color){255, 232, 180, 255} : mixColor(tint, white, 0.55f);
  float top = textTop();
  float erase = phase == PHASE_ERASE ? phaseT / ERASE_TIME : 0.0f;
  float leaving = phase == PHASE_EXIT ? clamp(1.0f - phaseT / (EXIT_TIME * 0.5f), 0.0f, 1.0f) : 1.0f;

  for (int i = 0; i < glyphCount; i++)
  {
    const Glyph *g = &glyphs[i];
    SDL_Texture *tex = (unsigned char)g->code >= GLYPH_FIRST && (unsigned char)g->code <= GLYPH_LAST
                           ? glyphTextures[g->code - GLYPH_FIRST]
                           : NULL;

    if (g->born < 0 || g->code == ' ' || tex == NULL)
    {
      continue;
    }

    float age = sceneT - g->born;

    if (age < 0)
    {
      continue;
    }

    float heat = expf(-age * 6.0f);
    float grow = 1.0f + 0.7f * expf(-age * 16.0f);
    float ga = clamp(age * 14.0f, 0.0f, 1.0f) * a * leaving;
    float dx = 0, dy = 0;

    // Dissolving: from the left of each line to the right, drifting up.
    if (erase > 0)
    {
      float len = (float)SDL_max(lineLength[g->line], 1);
      float e = clamp(erase * 1.7f - (g->col / len) * 0.7f, 0.0f, 1.0f);

      ga *= 1.0f - e;
      dy = -e * 16.0f;
      dx = (float)((i * 37) % 9 - 4) * e * 1.5f;
    }

    if (script.glitch > 0.2f && frand() < script.glitch * 0.04f)
    {
      dx += frandRange(-3.0f, 3.0f);
    }

    if (ga <= 0.004f)
    {
      continue;
    }

    float x = SCREEN_WIDTH / 2.0f - lineLength[g->line] * glyphW / 2.0f + g->col * glyphW;
    float y = top + g->line * LINE_STEP;
    float cx = x + glyphW / 2.0f + dx;
    float cy = y + glyphH / 2.0f + dy;
    float w = glyphW * grow;
    float h = glyphH * grow;

    if (heat > 0.05f)
    {
      drawGlowAt(cx, cy, 46.0f, tint, 0.55f * heat * ga);
    }

    SDL_Color c = mixColor(settled, white, heat);
    SDL_FRect dst = {cx - w / 2.0f, cy - h / 2.0f, w, h};

    SDL_SetTextureColorMod(tex, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(tex, (Uint8)(255 * ga));
    SDL_RenderTexture(renderer, tex, NULL, &dst);
    SDL_SetTextureColorMod(tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(tex, 255);
  }

  // Waiting to be turned: a small arrow after the last word, blinking.
  if (phase == PHASE_HOLD && lineCount > 0 && fmodf(phaseT, 0.8f) < 0.55f)
  {
    int last = lineCount - 1;
    float x = SCREEN_WIDTH / 2.0f + lineLength[last] * glyphW / 2.0f + 14.0f;
    float y = top + last * LINE_STEP + glyphH / 2.0f;

    vecLine(x, y - 6, x + 8, y, 1.2f, settled, 0.9f * a);
    vecLine(x + 8, y, x, y + 6, 1.2f, settled, 0.9f * a);
    vecLine(x, y + 6, x, y - 6, 1.2f, settled, 0.9f * a);
  }
}

static void drawCentred(SDL_Texture *tex, float y, float a)
{
  if (tex == NULL || a <= 0.004f)
  {
    return;
  }

  SDL_FPoint size = getSize(tex);
  float maxW = SCREEN_WIDTH - 40.0f;
  float scale = size.x > maxW ? maxW / size.x : 1.0f;
  SDL_FRect dst = {SCREEN_WIDTH / 2.0f - size.x * scale / 2.0f, y, size.x * scale,
                   size.y * scale};

  SDL_SetTextureAlphaMod(tex, (Uint8)(255 * clamp(a, 0.0f, 1.0f)));
  SDL_RenderTexture(renderer, tex, NULL, &dst);
  SDL_SetTextureAlphaMod(tex, 255);
}

// The run so far, as a line of nodes: a square for each level and a diamond
// for each fight, lit up to the one about to be played, which pulses. The four
// acts of an ordinary run, or the one endless act it is in.
static void drawMap(float a)
{
  drawCentred(mapTitle, MAP_TITLE_Y, a);
  drawCentred(mapDetail, MAP_DETAIL_Y, a);

  bool endless = context.act >= RUN_ACTS;
  int firstAct = endless ? context.act : 0;
  int acts = endless ? 1 : RUN_ACTS;
  int current = context.act * RUN_STAGES_PER_ACT + context.stageInAct;
  const float nodeGap = 34.0f;
  const float actGap = 60.0f;
  float actWidth = (RUN_STAGES_PER_ACT - 1) * nodeGap;
  float x0 = SCREEN_WIDTH / 2.0f - (acts * actWidth + (acts - 1) * actGap) / 2.0f;
  float y = MAP_NODES_Y;
  SDL_Color lit = mixColor(tint, white, 0.35f);
  SDL_Color dim = mixColor(backdrop, tint, 0.45f);
  float pulse = 0.5f + 0.5f * sinf(sceneT * 5.0f);
  float prevX = 0, prevR = 0;
  float nextX = -1, nextR = 0;

  for (int act = 0; act < acts; act++)
  {
    for (int s = 0; s < RUN_STAGES_PER_ACT; s++)
    {
      int stage = (firstAct + act) * RUN_STAGES_PER_ACT + s;
      float x = x0 + act * (actWidth + actGap) + s * nodeGap;
      bool boss = s == RUN_STAGES_PER_ACT - 1;
      bool done = stage < current;
      bool next = stage == current;
      float r = boss ? 9.0f : 5.0f;

      if (act > 0 || s > 0)
      {
        vecLine(prevX + prevR + 4, y, x - r - 4, y, 0.9f, done || next ? lit : dim,
                (done || next ? 0.7f : 0.35f) * a);
      }

      vecPolygon(x, y, r, 4, boss ? 0.0f : SDL_PI_F / 4, 1.2f, done || next ? lit : dim,
                 (done || next ? 1.0f : 0.6f) * a);

      if (done)
      {
        vecPolygon(x, y, r * 0.45f, 4, boss ? 0.0f : SDL_PI_F / 4, 1.2f, lit, a);
      }

      if (next)
      {
        vecPolygon(x, y, r + 4.0f + pulse * 3.0f, 4, boss ? 0.0f : SDL_PI_F / 4, 1.1f,
                   white, (0.4f + 0.5f * pulse) * a);
        nextX = x;
        nextR = r;
      }

      prevX = x;
      prevR = r;
    }
  }

  vecFlush();

  if (nextX >= 0)
  {
    drawGlowAt(nextX, y, 40.0f + nextR * 2.0f, tint, (0.4f + 0.3f * pulse) * a);
  }
}

void drawStory(void)
{
  if (!active)
  {
    return;
  }

  float a = alpha;

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, backdrop.r, backdrop.g, backdrop.b, (Uint8)(255 * a));
  SDL_FRect full = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
  SDL_RenderFillRect(renderer, &full);

  // A pool of its light behind it.
  drawGlowAt(being.x, being.y, 600.0f, tint, 0.18f * a);

  drawTunnel(&tunnel, tint, a, SCREEN_WIDTH / 2.0f + (being.x - SCREEN_WIDTH / 2.0f) * 0.25f,
             200.0f + (being.y - BEING_Y) * 0.2f);

  drawBeing(&being, tint, a);

  // The beam leaves from the eye in its forehead - unless the page is being
  // written by nobody, after the face has gone.
  bool silent = (currentPage()->flags & STORY_PAGE_SILENT) != 0;
  drawPen(&pen, silent || being.shattered ? NULL : &being.penOrigin, tint, a);

  drawText(a);

  if (script.showMap)
  {
    drawMap(a);
  }

  drawCentred(hintText, SCREEN_HEIGHT - 26.0f, a * (sceneT > INPUT_DELAY ? 1.0f : 0.4f));

  if (flash > 0)
  {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
    SDL_SetRenderDrawColor(renderer, 255, 240, 220, (Uint8)(220 * flash * flash * a));
    SDL_RenderFillRect(renderer, &full);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  }
}
