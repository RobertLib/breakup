#include "menu-screen.h"
#include "../lib/audio.h"
#include "../lib/game-state.h"
#include "../lib/gfx.h"
#include "../lib/postfx.h"
#include "../lib/save.h"
#include "../lib/starfield.h"
#include "../lib/transition.h"
#include "../paddle/paddle.h"
#include "../run/perks.h"
#include "../run/run.h"
#include "../lib/tunnel.h"
#include "../lib/vector.h"
#include "../story/being.h"
#include "../story/pen.h"
#include "../story/story.h"
#include "../types.h"

typedef enum MenuPage
{
  PAGE_MAIN,
  PAGE_OPTIONS,
  PAGE_SCORES,
  PAGE_PERKS
} MenuPage;

// The rows this page can hold, and the rows this build shows. They are the same
// number everywhere but the browser, where there is no quitting to offer and
// QUIT is the last of the six - so it is simply never rendered, never walked
// to and never clickable, and nothing else on the page changes. See
// BREAKUP_OFFERS_QUIT in globals.h.
//
// The arrays stay MAIN_ITEM_MAX long: the string is still in the binary, which
// costs five bytes and keeps the table and the `case ITEM_QUIT:` below reading
// as one list rather than as a list with a hole in it.
#define MAIN_ITEM_MAX 6
#define MAIN_ITEM_COUNT (BREAKUP_OFFERS_QUIT ? MAIN_ITEM_MAX : MAIN_ITEM_MAX - 1)
#define OPTION_ITEM_COUNT 5

// The rows of the main page by name, because three other pages hand the
// cursor back to the row that opened them.
enum
{
  ITEM_START,
  ITEM_DAILY,
  ITEM_PERKS,
  ITEM_SCORES,
  ITEM_OPTIONS,
  ITEM_QUIT
};

static const char *mainItems[MAIN_ITEM_MAX] = {
    "START GAME", "DAILY RUN", "PERKS", "HIGH SCORES", "OPTIONS", "QUIT"};

// The perk collection is two columns of names, read top to bottom.
#define PERK_ROWS 7
#define PERK_TOP 244.0f
#define PERK_STEP 30.0f

static const char *optionItems[OPTION_ITEM_COUNT] = {
    "MUSIC", "SOUND FX", "FULLSCREEN", "STORY", "BACK"};

static SDL_Texture *logoText;
static SDL_Texture *mainTextures[MAIN_ITEM_MAX];
static SDL_Texture *optionTextures[OPTION_ITEM_COUNT];
static SDL_Texture *scoresTitle;
static SDL_Texture *scoreRows[HIGH_SCORE_COUNT];
static SDL_Texture *scoresBack;
static SDL_Texture *onText;
static SDL_Texture *offText;
static SDL_Texture *hintText;

static SDL_Texture *dailyText; // today's daily, under the table of best runs
static SDL_Texture *perksTitle;
static SDL_Texture *perkNames[PERK_COUNT];
static SDL_Texture *perkDetail;
static int perkDetailFor = -1;

// The perks the collection lists, in enum order: every one but the instant.
static Perk collection[PERK_COUNT];
static int collectionSize;

static MenuPage page;
static int selection;

// The Sovereign, looming over the menu: big, dim, and watching whatever the
// cursor is on, with its eyes in the gap between the logo and the list. What
// it thinks of the choice shows on its face.
static Being face;
static Tunnel tunnel;

#define FACE_Y 222.0f
#define FACE_SCALE 150.0f

static const float MENU_TOP = 250;
static const float MENU_STEP = 50;

static void gotoPlaying(void)
{
  setStartLevel(startRun(false));
  storyRequestPrologue();
  nextGameState = GAME_STATE_PLAYING_SCREEN;
}

static void gotoDailyRun(void)
{
  setStartLevel(startRun(true));
  storyRequestPrologue();
  nextGameState = GAME_STATE_PLAYING_SCREEN;
}

// The collection's names are drawn from the save as it is when the page is
// opened, so they are rebuilt then rather than once for the whole menu: a perk
// unlocked since the menu last came up has to show as unlocked.
static void rebuildPerkPage(void)
{
  if (perksTitle != NULL)
  {
    SDL_DestroyTexture(perksTitle);
  }

  char buf[48];
  snprintf(buf, sizeof(buf), "PERKS  %d/%d", unlockedPerkCount(), perkCollectionSize());
  perksTitle = renderTextBlended(font32, buf, (SDL_Color){255, 255, 255, 255});

  for (int i = 0; i < collectionSize; i++)
  {
    if (perkNames[i] != NULL)
    {
      SDL_DestroyTexture(perkNames[i]);
    }

    const PerkInfo *info = perkInfo(collection[i]);
    bool unlocked = perkUnlocked(collection[i]);

    perkNames[i] = renderTextBlended(
        font16, info->name, unlocked ? info->color : (SDL_Color){90, 96, 125, 255});
  }

  perkDetailFor = -1;
}

// The box under the grid: what the highlighted perk does, or what unlocks it.
static void rebuildPerkDetail(void)
{
  if (perkDetail != NULL)
  {
    SDL_DestroyTexture(perkDetail);
    perkDetail = NULL;
  }

  perkDetailFor = selection;

  if (selection < 0 || selection >= collectionSize)
  {
    return;
  }

  const PerkInfo *info = perkInfo(collection[selection]);
  char buf[160];

  if (perkUnlocked(collection[selection]))
  {
    snprintf(buf, sizeof(buf), "%s\n%s", info->name, info->description);
    perkDetail = renderTextWrapped(font16, buf, (SDL_Color){210, 216, 240, 255}, 620, false);
  }
  else
  {
    snprintf(buf, sizeof(buf), "%s - LOCKED\nTO UNLOCK: %s", info->name,
             info->unlockHint != NULL ? info->unlockHint : "?");
    perkDetail = renderTextWrapped(font16, buf, (SDL_Color){255, 170, 90, 255}, 620, false);
  }
}

static SDL_FRect perkCellRect(int index)
{
  int col = index / PERK_ROWS;
  int row = index % PERK_ROWS;

  return (SDL_FRect){col == 0 ? 90.0f : 450.0f, PERK_TOP + row * PERK_STEP, 300, 24};
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
      snprintf(buf, sizeof(buf), "%d.   %06d   ACT %d",
               i + 1, saveData.highScores[i], saveData.highActs[i]);
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

  if (dailyText != NULL)
  {
    SDL_DestroyTexture(dailyText);
  }

  char buf[64];

  if (saveData.dailyDate != 0 && saveData.dailyDate == todayStamp())
  {
    snprintf(buf, sizeof(buf), "TODAY'S DAILY  BEST %06d", saveData.dailyBest);
  }
  else
  {
    snprintf(buf, sizeof(buf), "TODAY'S DAILY  NOT PLAYED YET");
  }

  dailyText = renderTextBlended(font16, buf, (SDL_Color){140, 235, 255, 255});
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

  collectionSize = 0;

  for (int i = 0; i < PERK_COUNT; i++)
  {
    if (!perkInfo((Perk)i)->instant)
    {
      collection[collectionSize++] = (Perk)i;
    }
  }

  beingReset(&face);
  beingPlace(&face, SCREEN_WIDTH / 2.0f, FACE_Y);
  face.scale = FACE_SCALE;
  face.eyeGlow = 3.0f;
  face.wander = 1.4f;

  // Somebody who has broken it once meets it put back together, and it shows.
  beingSetGlitch(&face, saveData.storySeen & STORY_SEEN_FINALE ? 0.15f : 0.0f);

  tunnelReset(&tunnel);

  playMusic(MUSIC_MENU);
}

static void activateMainItem(int item)
{
  playSfx(SFX_MENU_SELECT);

  switch (item)
  {
  case ITEM_START:
    startTransition(gotoPlaying);
    break;
  case ITEM_DAILY:
    startTransition(gotoDailyRun);
    break;
  case ITEM_PERKS:
    rebuildPerkPage();
    page = PAGE_PERKS;
    selection = 0;
    break;
  case ITEM_SCORES:
    // Rebuilt on the way in, because the date the daily line is measured
    // against moves at midnight whether or not the menu does.
    rebuildScoreRows();
    page = PAGE_SCORES;
    break;
  case ITEM_OPTIONS:
    page = PAGE_OPTIONS;
    selection = 0;
    break;
  case ITEM_QUIT:
    quitRequested = true;
    break;
  }
}

static void backToMain(int row)
{
  playSfx(SFX_MENU_SELECT);
  page = PAGE_MAIN;
  selection = row;
}

static void leavePerkPage(void)
{
  backToMain(ITEM_PERKS);
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
  // The Sovereign's scenes - the prologue, the interludes, the ending. A
  // toggle like FULLSCREEN, so like it this takes no auto-repeat.
  case 3:
    saveData.story = !saveData.story;
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
  case PAGE_PERKS:
    return collectionSize > 0 ? collectionSize : 1;
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
      backToMain(ITEM_SCORES);
    }
    return;
  }

  // Pointing at a name reads it; a click anywhere goes back, as on the scores.
  if (page == PAGE_PERKS)
  {
    for (int i = 0; i < collectionSize; i++)
    {
      SDL_FRect cell = perkCellRect(i);

      if (mouseMoved && selection != i &&
          motionX >= cell.x - 20 && motionX <= cell.x + cell.w &&
          motionY >= cell.y - 3 && motionY <= cell.y + cell.h + 3)
      {
        selection = i;
        playSfx(SFX_MENU_MOVE);
      }
    }

    if (isMousePressed[1])
    {
      leavePerkPage();
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
          backToMain(ITEM_OPTIONS);
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

// Where the cursor is, for the face to look at, and what it makes of it: the
// thought of a run amuses it, the table of the best of them does not, and
// being left is the one thing on the menu that it minds.
static void watchSelection(void)
{
  float step = (float)realDt;
  float y = MENU_TOP + selection * MENU_STEP + 16.0f;
  StoryMood mood = STORY_MOOD_CALM;

  if (page == PAGE_MAIN)
  {
    static const StoryMood moods[MAIN_ITEM_MAX] = {
        [ITEM_START] = STORY_MOOD_AMUSED, [ITEM_DAILY] = STORY_MOOD_AMUSED,
        [ITEM_PERKS] = STORY_MOOD_CALM, [ITEM_SCORES] = STORY_MOOD_STERN,
        [ITEM_OPTIONS] = STORY_MOOD_CALM, [ITEM_QUIT] = STORY_MOOD_SOFT};

    mood = moods[clamp(selection, 0, MAIN_ITEM_MAX - 1)];
  }
  else if (page == PAGE_SCORES)
  {
    mood = STORY_MOOD_STERN;
    y = 380.0f;
  }
  else if (page == PAGE_PERKS && collectionSize > 0)
  {
    SDL_FRect cell = perkCellRect(selection);
    y = cell.y + 8.0f;
  }

  beingSetMood(&face, mood);
  beingLookAt(&face, SCREEN_WIDTH / 2.0f - 140.0f, y);
  updateBeing(&face, step);
  updateTunnel(&tunnel, step);
}

void updateMenuScreen(void)
{
  watchSelection();

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
        backToMain(ITEM_OPTIONS);
      }
      else
      {
        adjustOption(selection, 1);
      }
    }

    if (isKeyPressed[K_ESCAPE])
    {
      backToMain(ITEM_OPTIONS);
    }
  }
  else if (page == PAGE_PERKS)
  {
    // Up and down walk the list; left and right jump a column.
    if (collectionSize > 0 && (isKeyRepeated[K_LEFT] || isKeyRepeated[K_RIGHT]))
    {
      selection = wrapIndex(selection + (isKeyRepeated[K_RIGHT] ? PERK_ROWS : -PERK_ROWS),
                            collectionSize);
      playSfx(SFX_MENU_MOVE);
    }

    if (isKeyPressed[K_ESCAPE] || isKeyPressed[K_RETURN] || isKeyPressed[K_SPACE])
    {
      leavePerkPage();
    }
  }
  else // PAGE_SCORES
  {
    if (isKeyPressed[K_ESCAPE] || isKeyPressed[K_RETURN] || isKeyPressed[K_SPACE])
    {
      backToMain(ITEM_SCORES);
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

    // The Sovereign's stylus either side of the choice, pointing at it the
    // way it points at a letter it is writing.
    SDL_Color glow = mixColor(worldThemes[WORLD_COUNT].glow, (SDL_Color){255, 255, 255, 255}, 0.3f);

    float cx = SCREEN_WIDTH / 2.0f;
    float cy = y + size.y / 2.0f;
    float offset = w / 2 + 28 + sinf(gameTime * 6.0f) * 3.0f;

    drawStylus(cx - offset, cy, 8.0f, gameTime, glow, 1.0f);
    drawStylus(cx + offset, cy, 8.0f, -gameTime, glow, 1.0f);
    vecFlush();

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

  // Sunk towards the dark of the Sovereign's scenes, with its tunnel and its
  // face over that, before anything that has to be read.
  SDL_Color glow = worldThemes[WORLD_COUNT].glow;

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 6, 6, 18, 110);
  SDL_FRect full = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
  SDL_RenderFillRect(renderer, &full);

  drawTunnel(&tunnel, glow, 0.55f, SCREEN_WIDTH / 2.0f, 290.0f);
  drawBeing(&face, glow, 0.24f);

  drawLogo();

  if (page == PAGE_MAIN)
  {
    for (int i = 0; i < MAIN_ITEM_COUNT; i++)
    {
      drawItem(mainTextures[i], i, selection == i);
    }
  }
  else if (page == PAGE_PERKS)
  {
    if (perkDetailFor != selection)
    {
      rebuildPerkDetail();
    }

    SDL_FPoint titleSize = getSize(perksTitle);
    SDL_FRect titleDst = {SCREEN_WIDTH / 2 - titleSize.x / 2, 192, titleSize.x, titleSize.y};
    SDL_RenderTexture(renderer, perksTitle, NULL, &titleDst);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    for (int i = 0; i < collectionSize; i++)
    {
      SDL_FRect cell = perkCellRect(i);
      const PerkInfo *info = perkInfo(collection[i]);
      bool unlocked = perkUnlocked(collection[i]);
      SDL_Color swatch = unlocked ? info->color : (SDL_Color){60, 64, 90, 255};

      // A swatch of the perk's colour, hollow while it is still locked
      SDL_SetRenderDrawColor(renderer, swatch.r, swatch.g, swatch.b, 255);
      SDL_FRect mark = {cell.x, cell.y + 4, 8, 8};

      if (unlocked)
      {
        SDL_RenderFillRect(renderer, &mark);
      }
      else
      {
        SDL_RenderRect(renderer, &mark);
      }

      SDL_FPoint size = getSize(perkNames[i]);
      SDL_FRect dst = {cell.x + 18, cell.y, size.x, size.y};

      if (i == selection)
      {
        SDL_Color glow = worldThemes[WORLD_COUNT].glow;
        float cx = cell.x - 12 + sinf(gameTime * 6.0f) * 2.0f;
        float cy = cell.y + 8;

        drawStylus(cx - 2, cy, 5.0f, gameTime, glow, 1.0f);
        vecFlush();
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
      }

      SDL_RenderTexture(renderer, perkNames[i], NULL, &dst);
    }

    if (perkDetail != NULL)
    {
      SDL_FPoint size = getSize(perkDetail);

      SDL_SetRenderDrawColor(renderer, 8, 10, 26, 200);
      SDL_FRect box = {SCREEN_WIDTH / 2.0f - 330, 458, 660, 80};
      SDL_RenderFillRect(renderer, &box);

      SDL_FRect dst = {SCREEN_WIDTH / 2.0f - 310, 470, size.x, size.y};
      SDL_RenderTexture(renderer, perkDetail, NULL, &dst);
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

        SDL_Color glow = mixColor(worldThemes[WORLD_COUNT].glow, (SDL_Color){255, 255, 255, 255}, 0.3f);
        drawStylus(SCREEN_WIDTH / 2.0f - 278 + sinf(gameTime * 6.0f) * 3.0f, y + size.y / 2,
                   8.0f, gameTime, glow, 1.0f);
        vecFlush();
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

    SDL_Texture *storyText = saveData.story ? onText : offText;
    SDL_FPoint storySize = getSize(storyText);
    SDL_FRect storyDst = {SCREEN_WIDTH / 2.0f + 110, MENU_TOP + 3 * MENU_STEP + 6, storySize.x, storySize.y};
    SDL_RenderTexture(renderer, storyText, NULL, &storyDst);
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

    SDL_FPoint dailySize = getSize(dailyText);
    SDL_FRect dailyDst = {SCREEN_WIDTH / 2 - dailySize.x / 2, 498, dailySize.x, dailySize.y};
    SDL_RenderTexture(renderer, dailyText, NULL, &dailyDst);

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

  SDL_DestroyTexture(dailyText);
  dailyText = NULL;

  for (int i = 0; i < PERK_COUNT; i++)
  {
    SDL_DestroyTexture(perkNames[i]);
    perkNames[i] = NULL;
  }

  SDL_DestroyTexture(perksTitle);
  SDL_DestroyTexture(perkDetail);
  perksTitle = NULL;
  perkDetail = NULL;
  perkDetailFor = -1;

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
