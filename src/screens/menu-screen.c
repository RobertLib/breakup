#include "menu-screen.h"
#include "../lib/audio.h"
#include "../lib/game-state.h"
#include "../lib/gfx.h"
#include "../lib/postfx.h"
#include "../lib/save.h"
#include "../lib/starfield.h"
#include "../lib/transition.h"
#include "../paddle/paddle.h"
#include "../types.h"

typedef enum MenuPage
{
  PAGE_MAIN,
  PAGE_OPTIONS,
  PAGE_SCORES
} MenuPage;

// The rows this page can hold, and the rows this build shows. They are the same
// number everywhere but the browser, where there is no quitting to offer and
// QUIT is the last of the five - so it is simply never rendered, never walked
// to and never clickable, and nothing else on the page changes. See
// BREAKUP_OFFERS_QUIT in globals.h.
//
// The arrays stay MAIN_ITEM_MAX long: the string is still in the binary, which
// costs five bytes and keeps the table and the `case 4:` below reading as one
// list rather than as a list with a hole in it.
#define MAIN_ITEM_MAX 5
#define MAIN_ITEM_COUNT (BREAKUP_OFFERS_QUIT ? MAIN_ITEM_MAX : MAIN_ITEM_MAX - 1)
#define OPTION_ITEM_COUNT 4

static const char *mainItems[MAIN_ITEM_MAX] = {
    "START GAME", "LEVEL SELECT", "HIGH SCORES", "OPTIONS", "QUIT"};

static const char *optionItems[OPTION_ITEM_COUNT] = {
    "MUSIC", "SOUND FX", "FULLSCREEN", "BACK"};

static SDL_Texture *logoText;
static SDL_Texture *mainTextures[MAIN_ITEM_MAX];
static SDL_Texture *optionTextures[OPTION_ITEM_COUNT];
static SDL_Texture *scoresTitle;
static SDL_Texture *scoreRows[HIGH_SCORE_COUNT];
static SDL_Texture *scoresBack;
static SDL_Texture *onText;
static SDL_Texture *offText;
static SDL_Texture *hintText;

static MenuPage page;
static int selection;

static const float MENU_TOP = 250;
static const float MENU_STEP = 50;

static void gotoPlaying(void)
{
  setStartLevel(0);
  nextGameState = GAME_STATE_PLAYING_SCREEN;
}

static void gotoLevelSelect(void)
{
  nextGameState = GAME_STATE_LEVEL_SELECT_SCREEN;
}

static void rebuildScoreRows(void)
{
  for (int i = 0; i < HIGH_SCORE_COUNT; i++)
  {
    if (scoreRows[i] != NULL)
    {
      SDL_DestroyTexture(scoreRows[i]);
    }

    char buf[64];

    if (saveData.highScores[i] > 0)
    {
      snprintf(buf, sizeof(buf), "%d.   %06d   LEVEL %d",
               i + 1, saveData.highScores[i], saveData.highLevels[i]);
    }
    else
    {
      snprintf(buf, sizeof(buf), "%d.   ------", i + 1);
    }

    scoreRows[i] = renderTextBlended(
        font24, buf,
        i == 0 && saveData.highScores[0] > 0
            ? (SDL_Color){255, 220, 90, 255}
            : (SDL_Color){190, 200, 230, 255});
  }
}

void initializeMenuScreen(void)
{
  page = PAGE_MAIN;
  selection = 0;

  logoText = renderTextBlended(font64, "BREAKUP", (SDL_Color){255, 255, 255, 255});

  for (int i = 0; i < MAIN_ITEM_COUNT; i++)
  {
    mainTextures[i] = renderTextBlended(font32, mainItems[i], (SDL_Color){255, 255, 255, 255});
  }

  for (int i = 0; i < OPTION_ITEM_COUNT; i++)
  {
    optionTextures[i] = renderTextBlended(font32, optionItems[i], (SDL_Color){255, 255, 255, 255});
  }

  scoresTitle = renderTextBlended(font32, "HIGH SCORES", (SDL_Color){255, 255, 255, 255});
  scoresBack = renderTextBlended(font24, "ESC / ENTER  --  BACK", (SDL_Color){150, 160, 200, 255});
  onText = renderTextBlended(font24, "ON", (SDL_Color){120, 255, 150, 255});
  offText = renderTextBlended(font24, "OFF", (SDL_Color){255, 120, 110, 255});
  hintText = renderTextBlended(
      font16,
      "ARROWS - NAVIGATE    ENTER - SELECT",
      (SDL_Color){110, 120, 160, 255});

  rebuildScoreRows();

  playMusic(MUSIC_MENU);
}

static void activateMainItem(int item)
{
  playSfx(SFX_MENU_SELECT);

  switch (item)
  {
  case 0:
    startTransition(gotoPlaying);
    break;
  case 1:
    startTransition(gotoLevelSelect);
    break;
  case 2:
    page = PAGE_SCORES;
    break;
  case 3:
    page = PAGE_OPTIONS;
    selection = 0;
    break;
  case 4:
    quitRequested = true;
    break;
  }
}

static void adjustOption(int item, int direction)
{
  switch (item)
  {
  // Both sliders step out and back in integers, and both do nothing at all
  // when the step would not move. That guard is what makes them safe to hold:
  // a bar parked at either end would otherwise write the save file and play a
  // blip on every one of the keyboard's thirty repeats a second.
  case 0:
  {
    int current = (int)roundf(saveData.musicVol * 10);
    int step = clamp(current + direction, 0, 10);

    if (step == current)
    {
      break;
    }

    saveData.musicVol = step / 10.0f;
    setMusicVolume(saveData.musicVol);
    writeSave();
    playSfx(SFX_MENU_MOVE);
    break;
  }
  case 1:
  {
    int current = (int)roundf(saveData.sfxVol * 10);
    int step = clamp(current + direction, 0, 10);

    if (step == current)
    {
      break;
    }

    saveData.sfxVol = step / 10.0f;
    setSfxVolume(saveData.sfxVol);
    writeSave();
    playSfx(SFX_MENU_SELECT); // demo the new volume
    break;
  }
  case 2:
    saveData.fullscreen = !saveData.fullscreen;
    SDL_SetWindowFullscreen(window, saveData.fullscreen);
    writeSave();
    playSfx(SFX_MENU_MOVE);
    break;
  }
}

static int itemCountForPage(void)
{
  switch (page)
  {
  case PAGE_MAIN:
    return MAIN_ITEM_COUNT;
  case PAGE_OPTIONS:
    return OPTION_ITEM_COUNT;
  default:
    return 1;
  }
}

static void handleMouse(void)
{
  if (page == PAGE_SCORES)
  {
    if (isMousePressed[1])
    {
      playSfx(SFX_MENU_SELECT);
      page = PAGE_MAIN;
      selection = 2;
    }
    return;
  }

  int count = itemCountForPage();

  for (int i = 0; i < count; i++)
  {
    float y = MENU_TOP + i * MENU_STEP;
    SDL_Texture *tex = page == PAGE_MAIN ? mainTextures[i] : optionTextures[i];
    SDL_FPoint size = getSize(tex);

    // Generous hit box centered on the item
    if (motionY >= y - 8 && motionY <= y + size.y + 8 &&
        motionX >= SCREEN_WIDTH / 2 - 220 && motionX <= SCREEN_WIDTH / 2 + 220)
    {
      if (mouseMoved && selection != i)
      {
        selection = i;
        playSfx(SFX_MENU_MOVE);
      }

      // One click is one action, and the loop stops here whatever it was.
      // `count` and the array this indexes both come from `page`, which
      // activating an item is free to change: clicking OPTIONS or HIGH SCORES
      // used to leave the loop reading optionTextures[4] on the next turn,
      // one past the end of a four-item array, and handing whatever it found
      // there to getSize().
      if (isMousePressed[1])
      {
        if (page == PAGE_MAIN)
        {
          activateMainItem(i);
        }
        else if (i == OPTION_ITEM_COUNT - 1)
        {
          playSfx(SFX_MENU_SELECT);
          page = PAGE_MAIN;
          selection = 3;
        }
        else
        {
          adjustOption(i, 1);
        }

        return;
      }
    }
  }
}

void updateMenuScreen(void)
{
  if (isTransitionActive())
  {
    return;
  }

  int count = itemCountForPage();

  if (isKeyRepeated[K_DOWN])
  {
    selection = (selection + 1) % count;
    playSfx(SFX_MENU_MOVE);
  }
  if (isKeyRepeated[K_UP])
  {
    selection = (selection + count - 1) % count;
    playSfx(SFX_MENU_MOVE);
  }

  if (page == PAGE_MAIN)
  {
    if (isKeyPressed[K_RETURN] || isKeyPressed[K_SPACE])
    {
      activateMainItem(selection);
    }

#if BREAKUP_OFFERS_QUIT
    if (isKeyPressed[K_ESCAPE])
    {
      quitRequested = true;
    }
#endif
  }
  else if (page == PAGE_OPTIONS)
  {
    // The two volume rows take the keyboard's auto-repeat, so a slider can be
    // held rather than tapped ten times. FULLSCREEN may not: repeating a
    // toggle thirty times a second is the strobe - an animated one, on macOS -
    // that the SDLK_F handler in main.c carries its own paragraph about.
    bool slider = selection == 0 || selection == 1;

    if (slider ? isKeyRepeated[K_LEFT] : isKeyPressed[K_LEFT])
    {
      adjustOption(selection, -1);
    }
    if (slider ? isKeyRepeated[K_RIGHT] : isKeyPressed[K_RIGHT])
    {
      adjustOption(selection, 1);
    }

    if (isKeyPressed[K_RETURN] || isKeyPressed[K_SPACE])
    {
      if (selection == OPTION_ITEM_COUNT - 1)
      {
        playSfx(SFX_MENU_SELECT);
        page = PAGE_MAIN;
        selection = 3;
      }
      else
      {
        adjustOption(selection, 1);
      }
    }

    if (isKeyPressed[K_ESCAPE])
    {
      playSfx(SFX_MENU_SELECT);
      page = PAGE_MAIN;
      selection = 3;
    }
  }
  else // PAGE_SCORES
  {
    if (isKeyPressed[K_ESCAPE] || isKeyPressed[K_RETURN] || isKeyPressed[K_SPACE])
    {
      playSfx(SFX_MENU_SELECT);
      page = PAGE_MAIN;
      selection = 2;
    }
  }

  handleMouse();
}

static void drawLogo(void)
{
  SDL_FPoint size = getSize(logoText);
  float bob = sinf(gameTime * 1.6f) * 5.0f;
  float x = SCREEN_WIDTH / 2 - size.x / 2;
  float y = 105 + bob;

  SDL_Color glow = worldThemes[WORLD_COUNT].glow;

  // Wide glow behind the logo
  SDL_SetTextureColorMod(texGlow, glow.r, glow.g, glow.b);
  SDL_SetTextureAlphaMod(texGlow, 60);
  SDL_FRect glowRect = {
      SCREEN_WIDTH / 2.0f - size.x * 0.75f,
      y + size.y / 2 - size.x * 0.3f,
      size.x * 1.5f,
      size.x * 0.6f};
  SDL_RenderTexture(renderer, texGlow, NULL, &glowRect);
  SDL_SetTextureColorMod(texGlow, 255, 255, 255);
  SDL_SetTextureAlphaMod(texGlow, 255);

  // Colored echo behind the white text
  SDL_SetTextureColorMod(logoText, glow.r, glow.g, glow.b);
  SDL_SetTextureAlphaMod(logoText, 120);
  SDL_FRect echo = {x + 3, y + 3, size.x, size.y};
  SDL_RenderTexture(renderer, logoText, NULL, &echo);

  SDL_SetTextureColorMod(logoText, 255, 255, 255);
  SDL_SetTextureAlphaMod(logoText, 255);
  SDL_FRect dst = {x, y, size.x, size.y};
  SDL_RenderTexture(renderer, logoText, NULL, &dst);
}

static void drawItem(SDL_Texture *tex, int index, bool selected)
{
  SDL_FPoint size = getSize(tex);
  float y = MENU_TOP + index * MENU_STEP;

  if (selected)
  {
    float pulse = 1.0f + 0.04f * sinf(gameTime * 6.0f);
    float w = size.x * pulse;
    float h = size.y * pulse;

    // Selection chevrons
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_Color glow = worldThemes[WORLD_COUNT].glow;
    SDL_SetRenderDrawColor(renderer, glow.r, glow.g, glow.b, 255);

    float cx = SCREEN_WIDTH / 2.0f;
    float cy = y + size.y / 2.0f;
    float offset = w / 2 + 26 + sinf(gameTime * 6.0f) * 3.0f;

    for (int i = 0; i < 3; i++)
    {
      SDL_RenderLine(renderer, cx - offset - i, cy - 7 + i, cx - offset + 7 - i, cy);
      SDL_RenderLine(renderer, cx - offset - i, cy + 7 - i, cx - offset + 7 - i, cy);
      SDL_RenderLine(renderer, cx + offset + i, cy - 7 + i, cx + offset - 7 + i, cy);
      SDL_RenderLine(renderer, cx + offset + i, cy + 7 - i, cx + offset - 7 + i, cy);
    }

    SDL_FRect dst = {cx - w / 2, cy - h / 2, w, h};
    SDL_RenderTexture(renderer, tex, NULL, &dst);
  }
  else
  {
    SDL_SetTextureColorMod(tex, 140, 150, 195);
    SDL_FRect dst = {SCREEN_WIDTH / 2 - size.x / 2, y, size.x, size.y};
    SDL_RenderTexture(renderer, tex, NULL, &dst);
    SDL_SetTextureColorMod(tex, 255, 255, 255);
  }
}

static void drawVolumeBar(int index, float value01)
{
  float y = MENU_TOP + index * MENU_STEP + 8;
  float x = SCREEN_WIDTH / 2.0f + 110;
  int steps = (int)roundf(value01 * 10);

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

  for (int i = 0; i < 10; i++)
  {
    if (i < steps)
    {
      SDL_SetRenderDrawColor(renderer, 120, 220, 255, 255);
    }
    else
    {
      SDL_SetRenderDrawColor(renderer, 60, 70, 110, 180);
    }

    SDL_FRect seg = {x + i * 14, y, 10, 16};
    SDL_RenderFillRect(renderer, &seg);
  }
}

void drawMenuScreen(void)
{
  // Drawn through the same offscreen pass the field is, for the bloom and
  // for nothing else: there is nothing here to shake.
  PostFx fx = {.zoom = 1.0f, .bloom = 0.70f};
  bool scene = beginScene();

  drawBackground(WORLD_COUNT, BACKGROUND_MENU, gameTime * 24.0f);
  drawLogo();

  if (page == PAGE_MAIN)
  {
    for (int i = 0; i < MAIN_ITEM_COUNT; i++)
    {
      drawItem(mainTextures[i], i, selection == i);
    }
  }
  else if (page == PAGE_OPTIONS)
  {
    for (int i = 0; i < OPTION_ITEM_COUNT; i++)
    {
      // Left-align labels so the bars fit next to them
      SDL_Texture *tex = optionTextures[i];
      SDL_FPoint size = getSize(tex);
      float y = MENU_TOP + i * MENU_STEP;

      bool selected = selection == i;

      if (selected)
      {
        SDL_FRect dst = {SCREEN_WIDTH / 2.0f - 250, y, size.x, size.y};
        SDL_RenderTexture(renderer, tex, NULL, &dst);
      }
      else
      {
        SDL_SetTextureColorMod(tex, 140, 150, 195);
        SDL_FRect dst = {SCREEN_WIDTH / 2.0f - 250, y, size.x, size.y};
        SDL_RenderTexture(renderer, tex, NULL, &dst);
        SDL_SetTextureColorMod(tex, 255, 255, 255);
      }
    }

    drawVolumeBar(0, saveData.musicVol);
    drawVolumeBar(1, saveData.sfxVol);

    SDL_Texture *fsText = saveData.fullscreen ? onText : offText;
    SDL_FPoint fsSize = getSize(fsText);
    SDL_FRect fsDst = {SCREEN_WIDTH / 2.0f + 110, MENU_TOP + 2 * MENU_STEP + 6, fsSize.x, fsSize.y};
    SDL_RenderTexture(renderer, fsText, NULL, &fsDst);
  }
  else // PAGE_SCORES
  {
    SDL_FPoint titleSize = getSize(scoresTitle);
    SDL_FRect titleDst = {SCREEN_WIDTH / 2 - titleSize.x / 2, 250, titleSize.x, titleSize.y};
    SDL_RenderTexture(renderer, scoresTitle, NULL, &titleDst);

    for (int i = 0; i < HIGH_SCORE_COUNT; i++)
    {
      SDL_FPoint size = getSize(scoreRows[i]);
      SDL_FRect dst = {SCREEN_WIDTH / 2 - size.x / 2, 310.0f + i * 38, size.x, size.y};
      SDL_RenderTexture(renderer, scoreRows[i], NULL, &dst);
    }

    SDL_FPoint backSize = getSize(scoresBack);
    SDL_FRect backDst = {SCREEN_WIDTH / 2 - backSize.x / 2, 530, backSize.x, backSize.y};
    SDL_RenderTexture(renderer, scoresBack, NULL, &backDst);
  }

  // Footer hint
  SDL_FPoint hintSize = getSize(hintText);
  SDL_FRect hintDst = {SCREEN_WIDTH / 2 - hintSize.x / 2, SCREEN_HEIGHT - 30, hintSize.x, hintSize.y};
  SDL_RenderTexture(renderer, hintText, NULL, &hintDst);

  if (scene)
  {
    endScene(&fx);
  }
}

void destroyMenuScreen(void)
{
  SDL_DestroyTexture(logoText);
  logoText = NULL;

  for (int i = 0; i < MAIN_ITEM_COUNT; i++)
  {
    SDL_DestroyTexture(mainTextures[i]);
    mainTextures[i] = NULL;
  }

  for (int i = 0; i < OPTION_ITEM_COUNT; i++)
  {
    SDL_DestroyTexture(optionTextures[i]);
    optionTextures[i] = NULL;
  }

  for (int i = 0; i < HIGH_SCORE_COUNT; i++)
  {
    if (scoreRows[i] != NULL)
    {
      SDL_DestroyTexture(scoreRows[i]);
      scoreRows[i] = NULL;
    }
  }

  SDL_DestroyTexture(scoresTitle);
  SDL_DestroyTexture(scoresBack);
  SDL_DestroyTexture(onText);
  SDL_DestroyTexture(offText);
  SDL_DestroyTexture(hintText);

  scoresTitle = NULL;
  scoresBack = NULL;
  onText = NULL;
  offText = NULL;
  hintText = NULL;
}
