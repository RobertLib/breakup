#include "game-over-screen.h"
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

static SDL_Texture *headerText;
static SDL_Texture *scoreText;
static SDL_Texture *levelText;
static SDL_Texture *highScoreText;
static SDL_Texture *infoText;

static bool isNewHighScore;

static void gotoMenu(void)
{
  nextGameState = GAME_STATE_MENU_SCREEN;
}

static void gotoRetry(void)
{
  setStartLevel(paddle.level);
  nextGameState = GAME_STATE_PLAYING_SCREEN;
}

void initializeGameOverScreen(void)
{
  stopMusic();
  playSfx(SFX_GAME_OVER);

  isNewHighScore = recordScore(paddle.score, paddle.level + 1) == 0;

  headerText = renderTextBlended(font64, "GAME OVER", (SDL_Color){255, 90, 90, 255});

  char buf[64];
  snprintf(buf, sizeof(buf), "SCORE  %06d", paddle.score);
  scoreText = renderTextBlended(font32, buf, (SDL_Color){255, 255, 255, 255});

  snprintf(buf, sizeof(buf), "REACHED LEVEL %d  -  %s",
           paddle.level + 1, getLevel(paddle.level)->name);
  levelText = renderTextBlended(font24, buf, (SDL_Color){190, 200, 230, 255});

  highScoreText = renderTextBlended(font24, "NEW HIGH SCORE!", (SDL_Color){255, 220, 90, 255});

  infoText = renderTextBlended(
      font16,
      "SPACE - MENU     R - RETRY LEVEL",
      (SDL_Color){150, 160, 200, 255});
}

void updateGameOverScreen(void)
{
  if (isTransitionActive())
  {
    return;
  }

  if (isKeyPressed[K_SPACE] || isKeyPressed[K_RETURN] ||
      isKeyPressed[K_ESCAPE] || isMousePressed[1])
  {
    playSfx(SFX_MENU_SELECT);
    startTransition(gotoMenu);
  }

  if (isKeyPressed[SDL_SCANCODE_R])
  {
    playSfx(SFX_MENU_SELECT);
    startTransition(gotoRetry);
  }
}

static void renderCopyCenter(SDL_Texture *texture, const int yOffset)
{
  if (texture != NULL)
  {
    SDL_FPoint size = getSize(texture);
    float maxW = SCREEN_WIDTH - 60;
    float scale = size.x > maxW ? maxW / size.x : 1.0f;
    float w = size.x * scale;
    float h = size.y * scale;
    SDL_FRect dstRect = {
        SCREEN_WIDTH / 2 - w / 2,
        SCREEN_HEIGHT / 2 - h / 2 + yOffset,
        w,
        h};
    SDL_RenderTexture(renderer, texture, NULL, &dstRect);
  }
}

void drawGameOverScreen(void)
{
  // Drawn through the same offscreen pass the field is, for the bloom and
  // for nothing else: there is nothing here to shake.
  PostFx fx = {.zoom = 1.0f, .bloom = 0.70f};
  bool scene = beginScene();

  drawBackground(worldForLevel(paddle.level), paddle.level, gameTime * 12.0f);

  renderCopyCenter(headerText, -120);
  renderCopyCenter(scoreText, -20);
  renderCopyCenter(levelText, 30);

  if (isNewHighScore && fmodf(gameTime, 0.8f) < 0.55f)
  {
    renderCopyCenter(highScoreText, 80);
  }

  renderCopyCenter(infoText, 150);

  if (scene)
  {
    endScene(&fx);
  }
}

void destroyGameOverScreen(void)
{
  SDL_Texture **all[] = {&headerText, &scoreText, &levelText, &highScoreText, &infoText};

  for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++)
  {
    SDL_DestroyTexture(*all[i]);
    *all[i] = NULL;
  }
}
