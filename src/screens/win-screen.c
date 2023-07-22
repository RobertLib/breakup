#include "win-screen.h"
#include "../lib/audio.h"
#include "../lib/camera.h"
#include "../lib/game-state.h"
#include "../lib/gfx.h"
#include "../lib/particles.h"
#include "../lib/postfx.h"
#include "../lib/save.h"
#include "../lib/starfield.h"
#include "../lib/transition.h"
#include "../paddle/paddle.h"
#include "../types.h"

static SDL_Texture *headerText;
static SDL_Texture *subText;
static SDL_Texture *scoreText;
static SDL_Texture *highScoreText;
static SDL_Texture *infoText;

static bool isNewHighScore;
static float fireworkTimer;

static void gotoMenu(void)
{
  nextGameState = GAME_STATE_MENU_SCREEN;
}

void initializeWinScreen(void)
{
  stopMusic();
  playSfx(SFX_WIN);

  camera.y = 0;
  camera.prevY = 0;
  clearParticles();
  fireworkTimer = 0.2f;

  isNewHighScore = recordScore(paddle.score, paddle.level + 1) == 0;

  headerText = renderTextBlended(font64, "YOU WIN!", (SDL_Color){255, 220, 90, 255});
  subText = renderTextBlended(font24, "THE CORE IS SHATTERED",
                              (SDL_Color){190, 200, 230, 255});

  char buf[64];
  snprintf(buf, sizeof(buf), "FINAL SCORE  %06d", paddle.score);
  scoreText = renderTextBlended(font32, buf, (SDL_Color){255, 255, 255, 255});

  highScoreText = renderTextBlended(font24, "NEW HIGH SCORE!", (SDL_Color){255, 220, 90, 255});

  infoText = renderTextBlended(
      font16,
      "SPACE - MENU",
      (SDL_Color){150, 160, 200, 255});
}

void updateWinScreen(void)
{
  // Fireworks!
  fireworkTimer -= (float)dt;

  if (fireworkTimer <= 0)
  {
    fireworkTimer = frandRange(0.35f, 0.8f);
    spawnFirework(frandRange(80, SCREEN_WIDTH - 80), frandRange(60, 300));
  }

  updateParticles();

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

void drawWinScreen(void)
{
  // Drawn through the same offscreen pass the field is, for the bloom and
  // for nothing else: there is nothing here to shake.
  PostFx fx = {.zoom = 1.0f, .bloom = 0.90f};
  bool scene = beginScene();

  drawBackground(WORLD_COUNT, gameTime * 24.0f);
  drawParticles();

  renderCopyCenter(headerText, -130);
  renderCopyCenter(subText, -55);
  renderCopyCenter(scoreText, 10);

  if (isNewHighScore && fmodf(gameTime, 0.8f) < 0.55f)
  {
    renderCopyCenter(highScoreText, 70);
  }

  renderCopyCenter(infoText, 150);

  if (scene)
  {
    endScene(&fx);
  }
}

void destroyWinScreen(void)
{
  clearParticles();

  SDL_Texture **all[] = {&headerText, &subText, &scoreText, &highScoreText, &infoText};

  for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++)
  {
    SDL_DestroyTexture(*all[i]);
    *all[i] = NULL;
  }
}
