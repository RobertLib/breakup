#include "level-select-screen.h"
#include "../level-types.h"
#include "../level-manager.h"
#include "../lib/audio.h"
#include "../lib/game-state.h"
#include "../lib/gfx.h"
#include "../lib/postfx.h"
#include "../lib/save.h"
#include "../lib/starfield.h"
#include "../lib/transition.h"
#include "../paddle/paddle.h"
#include "../types.h"

// The grid is five across at the size it was drawn, and goes wider rather than
// taller when there are more levels than that will hold. The README advertises
// that dropping another level file in makes it show up; at twenty-one levels
// the fifth row ran off the bottom of the screen and printed over the hint on
// its way, and at twenty-seven a sixth row does not fit at any height worth
// reading. Columns are the axis with room to give: the screen is wider than the
// space between the title and the hint, and a narrower card only costs the
// level name some width it was already being scaled into.
#define GRID_COLS_MIN 5
#define GRID_COLS_MAX 8

#define CARD_W 132.0f
#define CARD_H 84.0f
#define CARD_GAP 12.0f
#define GRID_TOP 130.0f

// The room the grid has, clear of the title above it and the hint below.
#define GRID_BOTTOM 556.0f

// Short enough to still read: the number and the name have to clear each other.
#define CARD_H_MIN 62.0f

// What the whole grid spans, whatever it is divided into.
#define GRID_WIDTH (GRID_COLS_MIN * CARD_W + (GRID_COLS_MIN - 1) * CARD_GAP)

static SDL_Texture *titleText;
static SDL_Texture *hintText;
static SDL_Texture **numberTexts;
static SDL_Texture **nameTexts;

static int selection;
static int levelCount;

// The fewest columns that fit every row on the screen, so the cards stay as
// large as the level count allows.
static int gridCols(void)
{
  for (int cols = GRID_COLS_MIN; cols < GRID_COLS_MAX; cols++)
  {
    int rows = (levelCount + cols - 1) / cols;

    if (rows * (CARD_H_MIN + CARD_GAP) <= GRID_BOTTOM - GRID_TOP)
    {
      return cols;
    }
  }

  return GRID_COLS_MAX;
}

static void gotoPlaying(void)
{
  setStartLevel(selection);
  nextGameState = GAME_STATE_PLAYING_SCREEN;
}

static void gotoMenu(void)
{
  nextGameState = GAME_STATE_MENU_SCREEN;
}

void initializeLevelSelectScreen(void)
{
  levelCount = getNumberOfLevels();
  selection = levelCount > 0 ? clamp(saveData.unlocked - 1, 0, levelCount - 1) : 0;

  titleText = renderTextBlended(font32, "SELECT LEVEL", (SDL_Color){255, 255, 255, 255});
  hintText = renderTextBlended(
      font16,
      "ENTER - PLAY    ESC - BACK",
      (SDL_Color){110, 120, 160, 255});

  // calloc(0, n) is allowed to hand back NULL, and a failed allocation looks
  // the same from here. Either way there is no grid to draw, and levelCount is
  // what every loop and every `% levelCount` below is written against - so it
  // is the one thing that has to agree with the arrays.
  if (levelCount > 0)
  {
    numberTexts = (SDL_Texture **)calloc((size_t)levelCount, sizeof(SDL_Texture *));
    nameTexts = (SDL_Texture **)calloc((size_t)levelCount, sizeof(SDL_Texture *));

    if (numberTexts == NULL || nameTexts == NULL)
    {
      fprintf(stderr, "Could not allocate the level select grid; showing none\n");
      free(numberTexts);
      free(nameTexts);
      numberTexts = NULL;
      nameTexts = NULL;
      levelCount = 0;
    }
  }

  for (int i = 0; i < levelCount; i++)
  {
    // Wide enough for any int, so that GCC does not have to take our word for
    // it: at 8 bytes `make win` warned about a truncation that cannot happen
    // (MAX_LEVELS is 100, so this is three digits at the most), and a warning
    // nobody can act on is one that trains you to skim the build log.
    char num[12];
    snprintf(num, sizeof(num), "%d", i + 1);
    numberTexts[i] = renderTextBlended(font32, num, (SDL_Color){255, 255, 255, 255});
    nameTexts[i] = renderTextBlended(font16, getLevel(i)->name, (SDL_Color){190, 200, 230, 255});
  }

  playMusic(MUSIC_MENU);
}

static SDL_FRect cardRect(int index)
{
  int cols = gridCols();
  int col = index % cols;
  int row = index / cols;
  int rows = (levelCount + cols - 1) / cols;

  if (rows < 1)
  {
    rows = 1;
  }

  // Whatever the column count, the grid spans the same width; the cards divide
  // it. Height keeps its design value until the rows stop fitting and then
  // gives up exactly as much as it has to.
  float cardW = (GRID_WIDTH - (cols - 1) * CARD_GAP) / cols;
  float pitch = fminf(CARD_H + CARD_GAP, (GRID_BOTTOM - GRID_TOP) / rows);
  float x0 = (SCREEN_WIDTH - GRID_WIDTH) / 2.0f;

  return (SDL_FRect){
      x0 + col * (cardW + CARD_GAP),
      GRID_TOP + row * pitch,
      cardW,
      pitch - CARD_GAP};
}

void updateLevelSelectScreen(void)
{
  if (isTransitionActive())
  {
    return;
  }

  // Every arrow below is a `% levelCount`, which divides by zero on a build
  // whose levels did not load. There is nothing to select in that case anyway -
  // but ESC still has to work, because a screen you cannot leave is worse than
  // an empty one.
  if (levelCount <= 0)
  {
    if (isKeyPressed[K_ESCAPE] || isKeyPressed[K_RETURN] ||
        isKeyPressed[K_SPACE] || isMousePressed[1])
    {
      playSfx(SFX_MENU_SELECT);
      startTransition(gotoMenu);
    }

    return;
  }

  int prev = selection;

  // Auto-repeat, so the grid can be held across rather than tapped
  // twenty-seven times. ENTER and ESC below stay edge-triggered.
  //
  // Through wrapIndex() rather than `%`, and the UP arrow is why: a row is
  // gridCols() wide, that has a floor of GRID_COLS_MIN whatever the level
  // count, and `(selection - step) % levelCount` is negative as soon as the
  // step is the larger of the two. See the note on wrapIndex() in globals.h.
  if (isKeyRepeated[K_RIGHT])
  {
    selection = wrapIndex(selection + 1, levelCount);
  }
  if (isKeyRepeated[K_LEFT])
  {
    selection = wrapIndex(selection - 1, levelCount);
  }
  if (isKeyRepeated[K_DOWN])
  {
    selection = wrapIndex(selection + gridCols(), levelCount);
  }
  if (isKeyRepeated[K_UP])
  {
    selection = wrapIndex(selection - gridCols(), levelCount);
  }

  // Mouse hover / click
  for (int i = 0; i < levelCount; i++)
  {
    SDL_FRect rect = cardRect(i);

    if (motionX >= rect.x && motionX <= rect.x + rect.w &&
        motionY >= rect.y && motionY <= rect.y + rect.h)
    {
      if (mouseMoved)
      {
        selection = i;
      }

      if (isMousePressed[1] && i < saveData.unlocked)
      {
        selection = i;
        playSfx(SFX_MENU_SELECT);
        startTransition(gotoPlaying);
        return;
      }
    }
  }

  if (selection != prev)
  {
    playSfx(SFX_MENU_MOVE);
  }

  if (isKeyPressed[K_RETURN] || isKeyPressed[K_SPACE])
  {
    if (selection < saveData.unlocked)
    {
      playSfx(SFX_MENU_SELECT);
      startTransition(gotoPlaying);
    }
    else
    {
      playSfx(SFX_POWERUP_BAD);
    }
  }

  if (isKeyPressed[K_ESCAPE])
  {
    playSfx(SFX_MENU_SELECT);
    startTransition(gotoMenu);
  }
}

static void drawPadlock(float cx, float cy)
{
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 150, 155, 180, 220);

  // Body
  SDL_FRect body = {cx - 7, cy - 2, 14, 11};
  SDL_RenderFillRect(renderer, &body);

  // Shackle
  SDL_FRect left = {cx - 5, cy - 9, 2, 7};
  SDL_FRect right = {cx + 3, cy - 9, 2, 7};
  SDL_FRect top = {cx - 5, cy - 10, 10, 2};
  SDL_RenderFillRect(renderer, &left);
  SDL_RenderFillRect(renderer, &right);
  SDL_RenderFillRect(renderer, &top);

  // Keyhole
  SDL_SetRenderDrawColor(renderer, 40, 44, 66, 255);
  SDL_FRect hole = {cx - 1, cy + 1, 2, 5};
  SDL_RenderFillRect(renderer, &hole);
}

void drawLevelSelectScreen(void)
{
  // Drawn through the same offscreen pass the field is, for the bloom and
  // for nothing else: there is nothing here to shake.
  PostFx fx = {.zoom = 1.0f, .bloom = 0.70f};
  bool scene = beginScene();

  drawBackground(WORLD_COUNT, gameTime * 24.0f);

  SDL_FPoint titleSize = getSize(titleText);
  SDL_FRect titleDst = {SCREEN_WIDTH / 2 - titleSize.x / 2, 55, titleSize.x, titleSize.y};
  SDL_RenderTexture(renderer, titleText, NULL, &titleDst);

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

  for (int i = 0; i < levelCount; i++)
  {
    SDL_FRect rect = cardRect(i);
    bool unlocked = i < saveData.unlocked;
    bool selected = i == selection;
    SDL_Color accent = worldThemes[worldForLevel(i)].glow;

    // Card background
    if (unlocked)
    {
      SDL_SetRenderDrawColor(renderer,
                             (Uint8)(accent.r * 0.16f),
                             (Uint8)(accent.g * 0.16f),
                             (Uint8)(accent.b * 0.16f),
                             225);
    }
    else
    {
      SDL_SetRenderDrawColor(renderer, 16, 18, 30, 225);
    }
    SDL_RenderFillRect(renderer, &rect);

    // Border
    if (selected)
    {
      float pulse = 0.6f + 0.4f * sinf(gameTime * 6.0f);
      SDL_SetRenderDrawColor(renderer, 255, 255, 255, (Uint8)(160 + 90 * pulse));

      SDL_FRect outer = {rect.x - 2, rect.y - 2, rect.w + 4, rect.h + 4};
      SDL_RenderRect(renderer, &outer);
      SDL_RenderRect(renderer, &rect);

      // Glow behind the selected card
      SDL_SetTextureColorMod(texGlow, accent.r, accent.g, accent.b);
      SDL_SetTextureAlphaMod(texGlow, 70);
      SDL_FRect glow = {rect.x - 30, rect.y - 30, rect.w + 60, rect.h + 60};
      SDL_RenderTexture(renderer, texGlow, NULL, &glow);
      SDL_SetTextureColorMod(texGlow, 255, 255, 255);
      SDL_SetTextureAlphaMod(texGlow, 255);
    }
    else
    {
      SDL_SetRenderDrawColor(renderer, accent.r, accent.g, accent.b,
                             unlocked ? 140 : 50);
      SDL_RenderRect(renderer, &rect);
    }

    if (unlocked)
    {
      SDL_FPoint numSize = getSize(numberTexts[i]);
      SDL_FRect numDst = {
          rect.x + rect.w / 2 - numSize.x / 2,
          rect.y + 12,
          numSize.x,
          numSize.y};
      SDL_RenderTexture(renderer, numberTexts[i], NULL, &numDst);

      // Scale the name down if it is wider than the card
      SDL_FPoint nameSize = getSize(nameTexts[i]);
      float maxW = rect.w - 10;
      float scale = nameSize.x > maxW ? maxW / nameSize.x : 1.0f;
      float w = nameSize.x * scale;
      float h = nameSize.y * scale;

      SDL_FRect nameDst = {
          rect.x + rect.w / 2 - w / 2,
          rect.y + rect.h - 16 - h / 2,
          w,
          h};
      SDL_RenderTexture(renderer, nameTexts[i], NULL, &nameDst);
    }
    else
    {
      SDL_SetTextureAlphaMod(numberTexts[i], 70);
      SDL_FPoint numSize = getSize(numberTexts[i]);
      SDL_FRect numDst = {
          rect.x + rect.w / 2 - numSize.x / 2,
          rect.y + 8,
          numSize.x,
          numSize.y};
      SDL_RenderTexture(renderer, numberTexts[i], NULL, &numDst);
      SDL_SetTextureAlphaMod(numberTexts[i], 255);

      drawPadlock(rect.x + rect.w / 2, rect.y + rect.h - 24);
    }
  }

  SDL_FPoint hintSize = getSize(hintText);
  SDL_FRect hintDst = {SCREEN_WIDTH / 2 - hintSize.x / 2, SCREEN_HEIGHT - 30, hintSize.x, hintSize.y};
  SDL_RenderTexture(renderer, hintText, NULL, &hintDst);

  if (scene)
  {
    endScene(&fx);
  }
}

void destroyLevelSelectScreen(void)
{
  SDL_DestroyTexture(titleText);
  SDL_DestroyTexture(hintText);
  titleText = NULL;
  hintText = NULL;

  // levelCount is zeroed above when either array is missing, so this loop and
  // the arrays it walks cannot disagree - but the guard is here because the
  // free() below is written to cope with NULL and this is not.
  if (numberTexts != NULL && nameTexts != NULL)
  {
    for (int i = 0; i < levelCount; i++)
    {
      SDL_DestroyTexture(numberTexts[i]);
      SDL_DestroyTexture(nameTexts[i]);
    }
  }

  free(numberTexts);
  free(nameTexts);
  numberTexts = NULL;
  nameTexts = NULL;
}
