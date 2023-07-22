#include "status-bar.h"
#include "../balls/balls.h"
#include "../boss/boss.h"
#include "../level-types.h"
#include "../level-manager.h"
#include "../lib/effects.h"
#include "../lib/gfx.h"
#include "../paddle/paddle.h"
#include "../types.h"

// The panel and the world's ceiling are the same edge; globals.h owns it.
#define PANEL_HEIGHT STATUS_BAR_HEIGHT

// How many mini paddles the bar has room for. An extra life caps at nine
// (see brick-item.c), so the ones past this are counted in a "+n" beside them
// rather than dropped - a reward the player cannot see is not a reward.
#define LIVES_ICON_MAX 6

static SDL_Texture *scoreText;
static SDL_Texture *comboText;
static SDL_Texture *levelText;
static SDL_Texture *livesExtraText;

static float displayScore;
static int shownScore = -1;
static int shownCombo = -1;
static int shownLevel = -1;
static int shownLivesExtra = -1;

static void rebuildScore(int value)
{
  if (scoreText != NULL)
  {
    SDL_DestroyTexture(scoreText);
  }

  char buf[32];
  snprintf(buf, sizeof(buf), "SCORE %06d", value);
  scoreText = renderTextBlended(font24, buf, (SDL_Color){255, 255, 255, 255});
  shownScore = value;
}

static void rebuildCombo(int mult)
{
  if (comboText != NULL)
  {
    SDL_DestroyTexture(comboText);
    comboText = NULL;
  }

  if (mult >= 2)
  {
    char buf[16];
    snprintf(buf, sizeof(buf), "x%d", mult);
    comboText = renderTextBlended(font24, buf, (SDL_Color){255, 220, 90, 255});
  }

  shownCombo = mult;
}

static void rebuildLivesExtra(int extra)
{
  if (livesExtraText != NULL)
  {
    SDL_DestroyTexture(livesExtraText);
    livesExtraText = NULL;
  }

  if (extra > 0)
  {
    char buf[8];
    snprintf(buf, sizeof(buf), "+%d", extra);
    livesExtraText = renderTextBlended(font16, buf, (SDL_Color){140, 235, 255, 255});
  }

  shownLivesExtra = extra;
}

static void rebuildLevel(void)
{
  if (levelText != NULL)
  {
    SDL_DestroyTexture(levelText);
  }

  int world = worldForLevel(paddle.level);
  char buf[64];

  if (isBossLevel(paddle.level))
  {
    snprintf(buf, sizeof(buf), "BOSS  %s", getLevel(paddle.level)->name);
  }
  else
  {
    snprintf(buf, sizeof(buf), "W%d-%d  %s",
             world + 1,
             levelInWorld(paddle.level),
             getLevel(paddle.level)->name);
  }

  levelText = renderTextBlended(font16, buf, (SDL_Color){190, 200, 230, 255});
  shownLevel = paddle.level;
}

void initializeStatusBar(void)
{
  displayScore = 0;
  shownScore = -1;
  shownCombo = -1;
  shownLevel = -1;
  shownLivesExtra = -1;

  rebuildScore(0);
  rebuildCombo(1);
  rebuildLevel();
  rebuildLivesExtra(0);
}

void updateStatusBar(void)
{
  // Score counts up smoothly
  displayScore += (paddle.score - displayScore) * fminf(1.0f, 10.0f * (float)dt);

  if (paddle.score - displayScore < 1.0f)
  {
    displayScore = paddle.score;
  }
}

static void drawEffectChips(void)
{
  typedef struct Chip
  {
    ItemType icon;
    float remaining;
    float total;
  } Chip;

  Chip chips[4];
  int numChips = 0;

  if (effects.fire > 0)
  {
    chips[numChips++] = (Chip){ITEM_FIRE, effects.fire, EFFECT_FIRE_DURATION};
  }
  if (effects.slow > 0)
  {
    chips[numChips++] = (Chip){ITEM_SLOW, effects.slow, EFFECT_SLOW_DURATION};
  }
  if (effects.shield > 0)
  {
    chips[numChips++] = (Chip){ITEM_SHIELD, effects.shield, EFFECT_SHIELD_DURATION};
  }

  float typeLeft = paddleTypeTimeLeft();
  if (typeLeft > 0)
  {
    ItemType icon = ITEM_LONG;

    switch (paddle.type)
    {
    case PADDLE_TYPE_LONG:
      icon = ITEM_LONG;
      break;
    case PADDLE_TYPE_SHOOTING:
      icon = ITEM_SHOOT;
      break;
    case PADDLE_TYPE_STICKY:
      icon = ITEM_STICKY;
      break;
    case PADDLE_TYPE_SHORT:
      icon = ITEM_SHRINK;
      break;
    default:
      typeLeft = 0;
      break;
    }

    if (typeLeft > 0)
    {
      chips[numChips++] = (Chip){icon, typeLeft, PADDLE_TYPE_DURATION};
    }
  }

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

  for (int i = 0; i < numChips; i++)
  {
    float x = 16 + i * 42;
    float y = PANEL_HEIGHT + 8;

    // Mini capsule icon
    SDL_FRect icon = {x, y, 30, 12};
    SDL_RenderTexture(renderer, texItem[chips[i].icon], NULL, &icon);

    // Remaining-time bar
    float frac = clamp(chips[i].remaining / chips[i].total, 0.0f, 1.0f);

    SDL_SetRenderDrawColor(renderer, 20, 24, 46, 200);
    SDL_FRect barBg = {x, y + 15, 30, 4};
    SDL_RenderFillRect(renderer, &barBg);

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 210);
    SDL_FRect bar = {x, y + 15, 30 * frac, 4};
    SDL_RenderFillRect(renderer, &bar);
  }
}

void drawStatusBar(void)
{
  int world = worldForLevel(paddle.level);
  SDL_Color accent = worldThemes[world].glow;

  // Rebuild cached textures when the values change
  if ((int)displayScore != shownScore)
  {
    rebuildScore((int)displayScore);
  }
  if (comboMultiplier() != shownCombo)
  {
    rebuildCombo(comboMultiplier());
  }
  if (paddle.level != shownLevel)
  {
    rebuildLevel();
  }

  int extraLives = paddle.lives > LIVES_ICON_MAX ? paddle.lives - LIVES_ICON_MAX : 0;

  if (extraLives != shownLivesExtra)
  {
    rebuildLivesExtra(extraLives);
  }

  // Translucent panel
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 8, 10, 26, 195);
  SDL_FRect panel = {0, 0, SCREEN_WIDTH, PANEL_HEIGHT};
  SDL_RenderFillRect(renderer, &panel);

  SDL_SetRenderDrawColor(renderer, accent.r, accent.g, accent.b, 130);
  SDL_FRect line = {0, PANEL_HEIGHT - 2, SCREEN_WIDTH, 2};
  SDL_RenderFillRect(renderer, &line);

  // Score
  SDL_FPoint scoreSize = getSize(scoreText);
  SDL_FRect scoreRect = {16, PANEL_HEIGHT / 2.0f - scoreSize.y / 2, scoreSize.x, scoreSize.y};
  SDL_RenderTexture(renderer, scoreText, NULL, &scoreRect);

  // Where the block on the left ends, which is what the level name below is
  // allowed to start after. The badge's *unpulsed* width is what counts: the
  // pulse adds a pixel or two at this size and the margin below absorbs them,
  // whereas laying the panel out from a figure that breathes would make the
  // level name breathe along with it.
  float leftBlockEnd = scoreRect.x + scoreSize.x;

  // Combo multiplier
  if (comboText != NULL)
  {
    SDL_FPoint comboSize = getSize(comboText);
    float pulse = 1.0f + 0.08f * sinf(gameTime * 10.0f);
    SDL_FRect comboRect = {
        26 + scoreSize.x,
        PANEL_HEIGHT / 2.0f - comboSize.y * pulse / 2,
        comboSize.x * pulse,
        comboSize.y * pulse};
    SDL_RenderTexture(renderer, comboText, NULL, &comboRect);
    leftBlockEnd = comboRect.x + comboSize.x;
  }

  // Lives as mini paddles (right side). Laid out before the level name is
  // placed, because they are the other thing it has to stay clear of.
  int iconsToDraw = paddle.lives > LIVES_ICON_MAX ? LIVES_ICON_MAX : paddle.lives;
  float iconW = 30, iconH = 8, gap = 7;
  float livesRight = SCREEN_WIDTH - 16;
  float livesLeft = iconsToDraw > 0
                        ? livesRight - iconW - (iconsToDraw - 1) * (iconW + gap)
                        : livesRight;

  SDL_SetTextureColorMod(texPaddle[0], 140, 235, 255);
  for (int i = 0; i < iconsToDraw; i++)
  {
    SDL_FRect icon = {
        livesRight - iconW - i * (iconW + gap),
        PANEL_HEIGHT / 2.0f - iconH / 2,
        iconW,
        iconH};
    SDL_RenderTexture(renderer, texPaddle[0], NULL, &icon);
  }
  SDL_SetTextureColorMod(texPaddle[0], 255, 255, 255);

  // The lives past the last icon, as "+n" to the left of the row. It moves
  // livesLeft with it, so the level name below still measures its room from
  // where the block actually begins.
  if (livesExtraText != NULL)
  {
    SDL_FPoint extraSize = getSize(livesExtraText);
    SDL_FRect extraDst = {
        livesLeft - gap - extraSize.x,
        PANEL_HEIGHT / 2.0f - extraSize.y / 2,
        extraSize.x,
        extraSize.y};
    SDL_RenderTexture(renderer, livesExtraText, NULL, &extraDst);

    livesLeft = extraDst.x;
  }

  // Level name: in the middle wherever there is room for it there, and out of
  // the way of the score when there is not.
  //
  // It was centred on SCREEN_WIDTH / 2 + 30 and capped at 210 wide, and those
  // two are fine apart and collide at six digits: "SCORE 033847" with an x5
  // beside it reaches past 325, which is where a full-width label begins, and
  // the two then print on top of each other. Which is not a corner case - the
  // combo *is* the scoring game, so the badge is up exactly when the score has
  // grown long, and it was a press capture of Nebula that made it obvious. So the
  // room is measured now, from where the left-hand block actually ended and where
  // the lives actually begin, and the name keeps its old place until that room
  // runs out: then it slides right, and only after that does it shrink. The
  // tightest thing the panel can be asked for - six lives, six digits and a badge
  // - leaves it about twenty pixels clear of the first life icon.
  const float margin = 12;
  float roomLeft = leftBlockEnd + margin;
  float roomRight = livesLeft - margin;

  SDL_FPoint levelSize = getSize(levelText);
  float maxLevelW = fminf(210.0f, fmaxf(0.0f, roomRight - roomLeft));
  float levelScale = levelSize.x > maxLevelW ? maxLevelW / levelSize.x : 1.0f;
  float levelW = levelSize.x * levelScale;
  float levelH = levelSize.y * levelScale;
  SDL_FRect levelRect = {
      clamp(SCREEN_WIDTH / 2 + 30 - levelW / 2, roomLeft, roomRight - levelW),
      PANEL_HEIGHT / 2 - levelH / 2,
      levelW,
      levelH};
  SDL_RenderTexture(renderer, levelText, NULL, &levelRect);

  drawEffectChips();
  drawBossBar();
}

void destroyStatusBar(void)
{
  SDL_Texture **all[] = {&scoreText, &comboText, &levelText, &livesExtraText};

  for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++)
  {
    if (*all[i] != NULL)
    {
      SDL_DestroyTexture(*all[i]);
      *all[i] = NULL;
    }
  }
}
