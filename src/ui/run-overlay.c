#include "run-overlay.h"
#include "../lib/audio.h"
#include "../lib/game-state.h"
#include "../lib/gfx.h"
#include "../paddle/paddle.h"
#include "../run/run.h"
#include "../lib/vector.h"
#include "../story/pen.h"
#include "../story/story.h"
#include "toast.h"

#define MAX_OPTIONS RUN_MAX_OFFERS

// Long enough that a player still hammering SPACE at the end of a level does
// not take whichever card happened to be highlighted, short enough not to
// feel like a wait.
#define INPUT_DELAY 0.6f
#define OPEN_ANIM 0.35f

#define CARD_X 70.0f
#define CARD_W 660.0f
#define CARD_PAD 22.0f
#define CARD_GAP 14.0f
#define CARD_TEXT_W ((int)(CARD_W - 2 * CARD_PAD))

typedef enum OverlayKind
{
  OVERLAY_NONE,
  OVERLAY_DRAFT,
  OVERLAY_CASHOUT
} OverlayKind;

typedef struct Option
{
  SDL_Texture *name;
  SDL_Texture *description;
  SDL_Texture *tag; // "INSTANT", or NULL
  SDL_Color color;
} Option;

static OverlayKind kind;
static float openTime;
static int selection;
static int optionCount;

static Perk offers[MAX_OPTIONS];
static Option options[MAX_OPTIONS];

static SDL_Texture *titleText;
static SDL_Texture *subtitleText;
static SDL_Texture *hintText;

static float cardsTop;

// Whether the cash-out on screen is the one after the last boss of the game.
static bool finale;

static void destroyTexture(SDL_Texture **texture)
{
  if (*texture != NULL)
  {
    SDL_DestroyTexture(*texture);
    *texture = NULL;
  }
}

static void destroyTextures(void)
{
  destroyTexture(&titleText);
  destroyTexture(&subtitleText);
  destroyTexture(&hintText);

  for (int i = 0; i < MAX_OPTIONS; i++)
  {
    destroyTexture(&options[i].name);
    destroyTexture(&options[i].description);
    destroyTexture(&options[i].tag);
  }

  optionCount = 0;
}

// Where option `index` is drawn, which is also where the mouse finds it. The
// cards are as tall as their text, so they are laid out top to bottom.
static SDL_FRect cardRect(int index)
{
  float y = cardsTop;
  SDL_FRect rect = {CARD_X, y, CARD_W, 0};

  for (int i = 0; i <= index && i < optionCount; i++)
  {
    float nameH = getSize(options[i].name).y;
    float descH = getSize(options[i].description).y;

    rect.y = y;
    rect.h = CARD_PAD + nameH + 10 + descH + CARD_PAD;
    y += rect.h + CARD_GAP;
  }

  return rect;
}

static void setHeader(const char *title, TTF_Font *titleFont, SDL_Color color,
                      const char *subtitle, const char *hint)
{
  titleText = renderTextBlended(titleFont, title, color);
  subtitleText = renderTextBlended(font16, subtitle, (SDL_Color){190, 200, 230, 255});
  hintText = renderTextBlended(font16, hint, (SDL_Color){110, 120, 160, 255});
}

static void begin(OverlayKind newKind, int selected)
{
  kind = newKind;
  selection = selected;
  openTime = 0;
}

static void closeAndAdvance(void)
{
  destroyTextures();
  kind = OVERLAY_NONE;

  int cleared = paddle.level;

  paddleGoToLevel(runAdvance());

  // The next stage is built and its music is playing: the Sovereign has its
  // say over that, and the level is waiting under it when it is done.
  storyPlayInterlude(cleared);
}

static void openDraft(void)
{
  destroyTextures();

  optionCount = runOfferPerks(offers, paddle.lives);

  // Only possible with every perk held and nine lives - in which case there
  // is nothing to choose and the run simply goes on.
  if (optionCount == 0)
  {
    closeAndAdvance();
    return;
  }

  char subtitle[96];
  snprintf(subtitle, sizeof(subtitle), "%s %d-%d CLEARED    SCORE X%.1f    PERKS HELD %d",
           runIsDaily() ? "DAILY" : "ACT", runAct() + 1, runStageInAct() + 1,
           runScoreMultiplier(), runPerkCount());

  char hint[64];
  snprintf(hint, sizeof(hint), "UP / DOWN - CHOOSE     ENTER OR 1-%d - TAKE",
           optionCount);

  setHeader("CHOOSE A PERK", font32, (SDL_Color){255, 255, 255, 255}, subtitle,
            optionCount > 1 ? hint : "ENTER - TAKE");

  for (int i = 0; i < optionCount; i++)
  {
    const PerkInfo *info = perkInfo(offers[i]);

    options[i].color = info->color;
    options[i].name = renderTextBlended(font24, info->name, info->color);
    options[i].description = renderTextWrapped(
        font16, info->description, (SDL_Color){200, 208, 235, 255}, CARD_TEXT_W, false);
    options[i].tag = info->instant
                         ? renderTextBlended(font16, "INSTANT", (SDL_Color){150, 160, 200, 255})
                         : NULL;
  }

  cardsTop = 146;
  begin(OVERLAY_DRAFT, 0);
}

static void openCashOut(void)
{
  destroyTextures();

  // The Sovereign at the end of the fourth act is the end of the game: the
  // victory bonus is paid the moment it falls, whichever way the player goes
  // from here, and the choice is to stop a winner or to carry on into the
  // endless acts.
  finale = runAtFinale();

  int bonus = finale ? runClaimVictory() : 0;
  paddle.score += bonus;

  char subtitle[96];

  if (finale)
  {
    snprintf(subtitle, sizeof(subtitle), "VICTORY BONUS +%d    SCORE %06d",
             bonus, paddle.score);
  }
  else
  {
    snprintf(subtitle, sizeof(subtitle), "ACT %d COMPLETE    SCORE %06d",
             runAct() + 1, paddle.score);
  }

  setHeader(finale ? "YOU WIN!" : "BOSS DOWN", font48, (SDL_Color){255, 220, 90, 255},
            subtitle, "UP / DOWN - CHOOSE     ENTER OR 1-2 - TAKE");

  char text[320];

  options[0].color = (SDL_Color){120, 255, 160, 255};
  options[0].name = renderTextBlended(font32, finale ? "FINISH" : "CASH OUT",
                                      options[0].color);
  snprintf(text, sizeof(text),
           finale ? "THE CORE IS SHATTERED. END THE RUN HERE WITH ALL %d POINTS."
                  : "END THE RUN HERE AND KEEP ALL %d POINTS.",
           paddle.score);
  options[0].description = renderTextWrapped(
      font16, text, (SDL_Color){200, 208, 235, 255}, CARD_TEXT_W, false);

  Curse curse = runNextCurse();
  char curseLine[128];

  if (curse != CURSE_COUNT)
  {
    snprintf(curseLine, sizeof(curseLine), "NEW CURSE: %s - %s.",
             curseName(curse), curseDescription(curse));
  }
  else
  {
    snprintf(curseLine, sizeof(curseLine), "NO NEW CURSE - YOU CARRY THEM ALL.");
  }

  options[1].color = (SDL_Color){255, 130, 90, 255};
  options[1].name = renderTextBlended(font32, finale ? "ENDLESS" : "GO DEEPER",
                                      options[1].color);

  // Where going on leads: the next act, or after the finale the endless ones.
  char where[48];

  if (finale)
  {
    snprintf(where, sizeof(where), "ON INTO THE ENDLESS ACTS");
  }
  else
  {
    snprintf(where, sizeof(where), "ACT %d", runAct() + 2);
  }

  snprintf(text, sizeof(text),
           "%s, SCORE X%.1f FROM NOW ON.\n%s\nDIE BEFORE YOU NEXT CASH OUT AND "
           "LOSE HALF OF WHAT YOU SCORE FROM HERE.",
           where, runNextScoreMultiplier(), curseLine);

  options[1].description = renderTextWrapped(
      font16, text, (SDL_Color){200, 208, 235, 255}, CARD_TEXT_W, false);

  optionCount = 2;
  cardsTop = 168;

  // GO DEEPER is where the cursor starts, so that the choice to stop is the
  // one taken on purpose - cashing out by accident ends the run. After the
  // finale it is the other way round: stopping there is the win, and the
  // endless run is the thing to choose deliberately.
  begin(OVERLAY_CASHOUT, finale ? 0 : 1);
}

void openRunOverlay(void)
{
  if (runStageIsBoss())
  {
    openCashOut();
  }
  else
  {
    openDraft();
  }
}

bool runOverlayActive(void)
{
  return kind != OVERLAY_NONE;
}

static void choose(int option)
{
  if (option < 0 || option >= optionCount)
  {
    return;
  }

  if (kind == OVERLAY_DRAFT)
  {
    Perk perk = offers[option];

    if (perk == PERK_EXTRA_LIFE)
    {
      paddle.lives = SDL_min(paddle.lives + 1, 9);
    }
    else
    {
      runTakePerk(perk);
    }

    playSfx(SFX_POWERUP_GOOD);
    closeAndAdvance();
    return;
  }

  if (option == 0)
  {
    playSfx(SFX_LEVEL_COMPLETE);
    destroyTextures();
    kind = OVERLAY_NONE;

    storyRequestEpilogue();
    runFinish(finale ? RUN_END_WON : RUN_END_CASHED, paddle.score);
    nextGameState = GAME_STATE_RUN_OVER_SCREEN;
    return;
  }

  Curse curse = runNextCurse();

  runGoDeeper(paddle.score);
  playSfx(SFX_POWERUP_BAD);

  if (curse != CURSE_COUNT)
  {
    char text[64];
    snprintf(text, sizeof(text), "CURSE: %s", curseName(curse));
    pushToast(text, (SDL_Color){255, 130, 90, 255});
  }

  // Going deeper is still a boss beaten, and that is worth a perk.
  openDraft();
}

void updateRunOverlay(void)
{
  if (kind == OVERLAY_NONE)
  {
    return;
  }

  openTime += (float)realDt;

  if (openTime < INPUT_DELAY || optionCount == 0)
  {
    return;
  }

  if (isKeyRepeated[K_DOWN])
  {
    selection = wrapIndex(selection + 1, optionCount);
    playSfx(SFX_MENU_MOVE);
  }

  if (isKeyRepeated[K_UP])
  {
    selection = wrapIndex(selection - 1, optionCount);
    playSfx(SFX_MENU_MOVE);
  }

  for (int i = 0; i < optionCount; i++)
  {
    if (isKeyPressed[SDL_SCANCODE_1 + i])
    {
      choose(i);
      return;
    }
  }

  if (isKeyPressed[K_RETURN] || isKeyPressed[K_SPACE])
  {
    choose(selection);
    return;
  }

  for (int i = 0; i < optionCount; i++)
  {
    SDL_FRect rect = cardRect(i);

    if (motionX < rect.x || motionX > rect.x + rect.w ||
        motionY < rect.y || motionY > rect.y + rect.h)
    {
      continue;
    }

    if (mouseMoved && selection != i)
    {
      selection = i;
      playSfx(SFX_MENU_MOVE);
    }

    if (isMousePressed[1])
    {
      choose(i);
    }

    return;
  }
}

static void drawCentered(SDL_Texture *texture, float y, Uint8 alpha)
{
  if (texture == NULL)
  {
    return;
  }

  SDL_FPoint size = getSize(texture);
  SDL_FRect dst = {SCREEN_WIDTH / 2.0f - size.x / 2, y, size.x, size.y};

  SDL_SetTextureAlphaMod(texture, alpha);
  SDL_RenderTexture(renderer, texture, NULL, &dst);
  SDL_SetTextureAlphaMod(texture, 255);
}

static void drawCard(int index, float appear)
{
  const Option *option = &options[index];
  SDL_FRect rect = cardRect(index);
  bool selected = index == selection;

  // Each card slides up into place a beat after the one above it.
  float t = clamp(appear * 1.6f - index * 0.25f, 0.0f, 1.0f);
  float ease = easeOutBack(t);
  rect.y += (1.0f - ease) * 40.0f;

  Uint8 alpha = (Uint8)(255 * t);
  SDL_Color c = option->color;

  if (selected)
  {
    float pulse = 0.75f + 0.25f * sinf(gameTime * 6.0f);

    SDL_SetTextureColorMod(texGlow, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(texGlow, (Uint8)(70 * pulse * t));
    SDL_FRect glow = {rect.x - 40, rect.y - 30, rect.w + 80, rect.h + 60};
    SDL_RenderTexture(renderer, texGlow, NULL, &glow);
    SDL_SetTextureColorMod(texGlow, 255, 255, 255);
    SDL_SetTextureAlphaMod(texGlow, 255);
  }

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 10, 12, 30, (Uint8)(235 * t));
  SDL_RenderFillRect(renderer, &rect);

  Uint8 edge = selected ? 255 : 90;
  SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, (Uint8)(edge * t));
  SDL_RenderRect(renderer, &rect);

  if (selected)
  {
    SDL_FRect inner = {rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2};
    SDL_RenderRect(renderer, &inner);

    // The Sovereign's stylus, as in the menus, on the left edge of the card,
    // and the corners of the card picked out the way its scenes frame things.
    float cx = rect.x - 20 + sinf(gameTime * 6.0f) * 3.0f;
    float cy = rect.y + rect.h / 2;
    float a = t;
    const float g = 7.0f;  // how far outside the card the corners sit
    const float arm = 18.0f;
    float x0 = rect.x - g, y0 = rect.y - g;
    float x1 = rect.x + rect.w + g, y1 = rect.y + rect.h + g;

    drawStylus(cx, cy, 8.0f, gameTime, c, a);

    vecLine(x0, y0, x0 + arm, y0, 1.2f, c, a);
    vecLine(x0, y0, x0, y0 + arm, 1.2f, c, a);
    vecLine(x1, y0, x1 - arm, y0, 1.2f, c, a);
    vecLine(x1, y0, x1, y0 + arm, 1.2f, c, a);
    vecLine(x0, y1, x0 + arm, y1, 1.2f, c, a);
    vecLine(x0, y1, x0, y1 - arm, 1.2f, c, a);
    vecLine(x1, y1, x1 - arm, y1, 1.2f, c, a);
    vecLine(x1, y1, x1, y1 - arm, 1.2f, c, a);
    vecFlush();
  }

  float textX = rect.x + CARD_PAD;
  float y = rect.y + CARD_PAD;

  SDL_FPoint nameSize = getSize(option->name);
  SDL_FRect nameDst = {textX, y, nameSize.x, nameSize.y};
  SDL_SetTextureAlphaMod(option->name, alpha);
  SDL_RenderTexture(renderer, option->name, NULL, &nameDst);
  SDL_SetTextureAlphaMod(option->name, 255);

  if (option->tag != NULL)
  {
    SDL_FPoint tagSize = getSize(option->tag);
    SDL_FRect tagDst = {rect.x + rect.w - CARD_PAD - tagSize.x,
                        y + (nameSize.y - tagSize.y) / 2, tagSize.x, tagSize.y};
    SDL_SetTextureAlphaMod(option->tag, alpha);
    SDL_RenderTexture(renderer, option->tag, NULL, &tagDst);
    SDL_SetTextureAlphaMod(option->tag, 255);
  }

  SDL_FPoint descSize = getSize(option->description);
  SDL_FRect descDst = {textX, y + nameSize.y + 10, descSize.x, descSize.y};
  SDL_SetTextureAlphaMod(option->description, alpha);
  SDL_RenderTexture(renderer, option->description, NULL, &descDst);
  SDL_SetTextureAlphaMod(option->description, 255);
}

void drawRunOverlay(void)
{
  if (kind == OVERLAY_NONE)
  {
    return;
  }

  float appear = clamp(openTime / OPEN_ANIM, 0.0f, 1.0f);

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 4, 6, 16, (Uint8)(200 * appear));
  SDL_FRect full = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
  SDL_RenderFillRect(renderer, &full);

  Uint8 alpha = (Uint8)(255 * appear);
  float titleY = kind == OVERLAY_CASHOUT ? 58 : 64;

  drawCentered(titleText, titleY, alpha);
  drawCentered(subtitleText, titleY + getSize(titleText).y + 14, alpha);

  for (int i = 0; i < optionCount; i++)
  {
    drawCard(i, appear);
  }

  // Dimmed until it will actually be listened to.
  drawCentered(hintText, SCREEN_HEIGHT - 34, openTime < INPUT_DELAY ? 90 : 255);
}

void destroyRunOverlay(void)
{
  destroyTextures();
  kind = OVERLAY_NONE;
}
