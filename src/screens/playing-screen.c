#include "playing-screen.h"
#include "../balls/balls.h"
#include "../boss/boss.h"
#include "../bricks/bricks.h"
#include "../bricks/brick-item.h"
#include "../enemies/enemies.h"
#include "../level-types.h"
#include "../level-manager.h"
#include "../lib/audio.h"
#include "../lib/camera.h"
#include "../lib/effects.h"
#include "../lib/game-state.h"
#include "../lib/gfx.h"
#include "../lib/particles.h"
#include "../lib/postfx.h"
#include "../lib/save.h"
#include "../lib/starfield.h"
#include "../paddle/paddle.h"
#include "../types.h"
#include "../ui/floating-text.h"
#include "../ui/status-bar.h"

#define PAUSE_ITEM_COUNT 3

static SDL_Texture *getReadyText;
static SDL_Texture *levelBannerText;
static SDL_Texture *levelCompleteText;
static SDL_Texture *bonusText;
static SDL_Texture *pausedText;
static SDL_Texture *pauseItems[PAUSE_ITEM_COUNT];

static int pauseSelection;
static int bannerLevel = -1;

static void rebuildLevelBanner(void)
{
  if (levelBannerText != NULL)
  {
    SDL_DestroyTexture(levelBannerText);
  }

  int world = worldForLevel(paddle.level);
  char buf[80];

  if (isBossLevel(paddle.level))
  {
    snprintf(buf, sizeof(buf), "BOSS  -  %s", getLevel(paddle.level)->name);
  }
  else
  {
    snprintf(buf, sizeof(buf), "WORLD %d  -  %s",
             world + 1, getLevel(paddle.level)->name);
  }

  levelBannerText = renderTextBlended(font32, buf, worldThemes[world].glow);
  bannerLevel = paddle.level;
}

static void rebuildBonusText(void)
{
  if (bonusText != NULL)
  {
    SDL_DestroyTexture(bonusText);
    bonusText = NULL;
  }

  if (paddle.levelBonus > 0)
  {
    char buf[48];
    snprintf(buf, sizeof(buf), "LIVES BONUS  +%d", paddle.levelBonus);
    bonusText = renderTextBlended(font24, buf, (SDL_Color){255, 220, 90, 255});
  }
}

void initializePlaying(void)
{
  initializePaddle();
  initializeBalls();
  initializeBricks();
  initializeEnemies();
  initializeBoss();
  initializeCamera();
  initializeStatusBar();

  resetEffects();
  resetBrickItems();
  clearParticles();
  clearFloatingTexts();

  isPause = false;
  pauseSelection = 0;
  bannerLevel = -1;

  getReadyText = renderTextBlended(font24, "GET READY", (SDL_Color){255, 255, 255, 255});
  levelCompleteText = renderTextBlended(font48, "LEVEL COMPLETE!", (SDL_Color){255, 255, 255, 255});
  pausedText = renderTextBlended(font48, "PAUSED", (SDL_Color){255, 255, 255, 255});

  pauseItems[0] = renderTextBlended(font24, "RESUME", (SDL_Color){255, 255, 255, 255});
  pauseItems[1] = renderTextBlended(font24, "RESTART LEVEL", (SDL_Color){255, 255, 255, 255});
  pauseItems[2] = renderTextBlended(font24, "QUIT TO MENU", (SDL_Color){255, 255, 255, 255});

  rebuildLevelBanner();

  playMusic(musicForLevel(paddle.level));
}

static void updatePauseMenu(void)
{
  if (isKeyRepeated[K_DOWN])
  {
    pauseSelection = (pauseSelection + 1) % PAUSE_ITEM_COUNT;
    playSfx(SFX_MENU_MOVE);
  }
  if (isKeyRepeated[K_UP])
  {
    pauseSelection = (pauseSelection + PAUSE_ITEM_COUNT - 1) % PAUSE_ITEM_COUNT;
    playSfx(SFX_MENU_MOVE);
  }

  if (isKeyPressed[K_RETURN] || isKeyPressed[K_SPACE])
  {
    playSfx(SFX_MENU_SELECT);

    switch (pauseSelection)
    {
    case 0:
      isPause = false;
      break;
    // Both of these end the run - a restart begins a new one from zero, the
    // same way the game over screen's R does - and recordScore() used to live
    // on the game over and win screens alone. A score good enough for the table
    // was thrown away by every exit that was not dying.
    case 1:
      isPause = false;
      recordScore(paddle.score, paddle.level + 1);
      setStartLevel(paddle.level);
      requestGameStateRestart();
      break;
    case 2:
      isPause = false;
      recordScore(paddle.score, paddle.level + 1);
      nextGameState = GAME_STATE_MENU_SCREEN;
      break;
    }
  }
}

void updatePlaying(void)
{
  // Pause toggle
  if ((isKeyPressed[K_ESCAPE] || isKeyPressed[SDL_SCANCODE_P]) &&
      !paddle.levelCompleted)
  {
    isPause = !isPause;
    pauseSelection = 0;
    playSfx(SFX_MENU_SELECT);
  }

  if (isPause)
  {
    updatePauseMenu();
    return;
  }

  paddleTimerUpdate();

  if (bannerLevel != paddle.level)
  {
    rebuildLevelBanner();
  }

  if (paddle.levelCompleted)
  {
    if (bonusText == NULL && paddle.levelBonus > 0)
    {
      rebuildBonusText();
    }

    // The world keeps sparkling behind the banner. updateCamera() is in here
    // for the shake rather than the scroll: the crystal that ends the level is
    // also the last thing to shake the screen, and a branch that never decayed
    // the trauma left it rattling under the banner until the level changed.
    updateCamera();
    updateBricks();
    updateBoss();
    updateParticles();
    updateFloatingTexts();
    updateEffects();
    updateStatusBar();
    return;
  }

  if (bonusText != NULL && !paddle.levelCompleted)
  {
    SDL_DestroyTexture(bonusText);
    bonusText = NULL;
  }

  // Development helper: BREAKUP_AUTOPLAY=1 makes the paddle track the ball
  static int autoplay = -1;
  if (autoplay < 0)
  {
    autoplay = SDL_getenv("BREAKUP_AUTOPLAY") != NULL;
  }
  if (autoplay)
  {
    float lowestY = -1e9f;
    float targetX = paddle.pos.x;

    for (int i = 0; i < BALL_COUNT; i++)
    {
      if (balls[i].active && !balls[i].docked && balls[i].pos.y > lowestY)
      {
        lowestY = balls[i].pos.y;
        targetX = balls[i].pos.x + BALL_SIZE / 2.0f;
      }
    }

    float halfW = paddleWidth() / 2.0f;
    paddle.pos.x = clamp(targetX, halfW, SCREEN_WIDTH - halfW);
    paddle.moveToX = -1;
  }

  updatePaddle();
  updateBalls();
  updateBricks();
  updateBoss();
  updateEnemies();
  updateBrickItems();
  updateCamera();
  updateEffects();
  updateParticles();
  updateFloatingTexts();
  updateStatusBar();
}

static void drawShieldBarrier(void)
{
  if (!shieldActive())
  {
    return;
  }

  // Blink during the last two seconds
  if (effects.shield < 2.0f && fmodf(effects.shield, 0.25f) < 0.1f)
  {
    return;
  }

  float y = SCREEN_HEIGHT - 14;
  float pulse = 0.7f + 0.3f * sinf(gameTime * 8.0f);

  SDL_SetTextureColorMod(texGlow, 190, 130, 255);
  SDL_SetTextureAlphaMod(texGlow, (Uint8)(120 * pulse));
  SDL_FRect glow = {-40, y - 16, SCREEN_WIDTH + 80, 36};
  SDL_RenderTexture(renderer, texGlow, NULL, &glow);

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 220, 180, 255, (Uint8)(200 * pulse));
  SDL_FRect core = {0, y, SCREEN_WIDTH, 3};
  SDL_RenderFillRect(renderer, &core);

  // Two energy pulses race along the barrier in opposite directions
  for (int i = 0; i < 2; i++)
  {
    float dir = i == 0 ? 1.0f : -1.0f;
    float px = fmodf(gameTime * 280.0f * dir + i * 400.0f, SCREEN_WIDTH + 160.0f);
    if (px < 0)
    {
      px += SCREEN_WIDTH + 160.0f;
    }
    px -= 80.0f;

    SDL_SetTextureColorMod(texGlow, 235, 200, 255);
    SDL_SetTextureAlphaMod(texGlow, 190);
    SDL_FRect blob = {px - 30, y - 13, 60, 30};
    SDL_RenderTexture(renderer, texGlow, NULL, &blob);
  }

  SDL_SetTextureColorMod(texGlow, 255, 255, 255);
  SDL_SetTextureAlphaMod(texGlow, 255);
}

// Brief additive full-screen flash after explosions and crystal breaks
static void drawImpactFlash(void)
{
  if (effects.flash <= 0)
  {
    return;
  }

  float t = effects.flash;
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
  SDL_SetRenderDrawColor(renderer,
                         effects.flashColor.r,
                         effects.flashColor.g,
                         effects.flashColor.b,
                         (Uint8)(60 * t * t));
  SDL_FRect full = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
  SDL_RenderFillRect(renderer, &full);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
}

static void drawCenteredTexture(SDL_Texture *tex, float y)
{
  SDL_FPoint size = getSize(tex);
  float maxW = SCREEN_WIDTH - 60;
  float scale = size.x > maxW ? maxW / size.x : 1.0f;
  float w = size.x * scale;
  float h = size.y * scale;

  SDL_FRect dst = {SCREEN_WIDTH / 2 - w / 2, y, w, h};
  SDL_RenderTexture(renderer, tex, NULL, &dst);
}

static void drawOverlayBackdrop(Uint8 alpha)
{
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 4, 6, 16, alpha);
  SDL_FRect full = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
  SDL_RenderFillRect(renderer, &full);
}

void drawPlaying(void)
{
  float shake = shakeAmount();

  PostFx fx = {0};
  cameraShake(&fx.offsetX, &fx.offsetY, &fx.angle);
  fx.zoom = 1.0f + 0.04f * cameraPunch();
  fx.bloom = 1.0f;

  // Only the heavy end of the shake pulls the colour channels apart. Below that
  // it is a permanent smear on everything rather than a sign that something
  // just went off.
  fx.split = clamp((shake - 0.25f) / 0.75f, 0.0f, 1.0f);

  // The field is drawn into an offscreen texture so it can be bloomed and
  // thrown about as one picture. If the renderer will not give us one, the
  // shake falls back to nudging the world down the screen the way it used to.
  bool scene = beginScene();
  float savedCameraY = camera.y;

  if (!scene)
  {
    camera.y += fx.offsetY;
  }

  drawBackground(worldForLevel(paddle.level), camera.y);

  drawBricks();
  drawBrickItems();
  drawBoss();
  drawEnemies();
  drawBalls();
  drawPaddle();
  drawParticles();
  drawFloatingTexts();
  drawShieldBarrier();
  drawImpactFlash();

  camera.y = savedCameraY;

  if (scene)
  {
    endScene(&fx);
  }

  // Everything from here on is read rather than played, so it stays still and
  // stays sharp: outside the scene pass, nothing shakes it and nothing blooms
  // it into being harder to read.
  drawStatusBar();

  if (!paddle.isReady && !isPause)
  {
    drawOverlayBackdrop(110);
    drawCenteredTexture(levelBannerText, SCREEN_HEIGHT / 2.0f - 50);

    if (fmodf(gameTime, 0.9f) < 0.62f)
    {
      drawCenteredTexture(getReadyText, SCREEN_HEIGHT / 2.0f + 14);
    }
  }

  if (paddle.levelCompleted)
  {
    drawOverlayBackdrop(110);
    drawCenteredTexture(levelCompleteText, SCREEN_HEIGHT / 2.0f - 60);

    if (bonusText != NULL)
    {
      drawCenteredTexture(bonusText, SCREEN_HEIGHT / 2.0f + 12);
    }
  }

  if (isPause)
  {
    drawOverlayBackdrop(170);
    drawCenteredTexture(pausedText, 180);

    for (int i = 0; i < PAUSE_ITEM_COUNT; i++)
    {
      SDL_Texture *tex = pauseItems[i];
      SDL_FPoint size = getSize(tex);
      float y = 300.0f + i * 44;

      if (i == pauseSelection)
      {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 110, 220, 255, 255);
        float cy = y + size.y / 2;
        float offset = size.x / 2 + 22;
        SDL_RenderLine(renderer, SCREEN_WIDTH / 2.0f - offset, cy - 6,
                       SCREEN_WIDTH / 2.0f - offset + 8, cy);
        SDL_RenderLine(renderer, SCREEN_WIDTH / 2.0f - offset, cy + 6,
                       SCREEN_WIDTH / 2.0f - offset + 8, cy);
        SDL_RenderLine(renderer, SCREEN_WIDTH / 2.0f + offset, cy - 6,
                       SCREEN_WIDTH / 2.0f + offset - 8, cy);
        SDL_RenderLine(renderer, SCREEN_WIDTH / 2.0f + offset, cy + 6,
                       SCREEN_WIDTH / 2.0f + offset - 8, cy);
      }
      else
      {
        SDL_SetTextureColorMod(tex, 140, 150, 195);
      }

      SDL_FRect dst = {SCREEN_WIDTH / 2 - size.x / 2, y, size.x, size.y};
      SDL_RenderTexture(renderer, tex, NULL, &dst);
      SDL_SetTextureColorMod(tex, 255, 255, 255);
    }
  }
}

void destroyPlaying(void)
{
  destroyPaddle();
  destroyBalls();
  destroyBricks();
  destroyEnemies();
  destroyStatusBar();

  clearParticles();
  clearFloatingTexts();

  SDL_Texture **all[] = {
      &getReadyText, &levelBannerText, &levelCompleteText,
      &bonusText, &pausedText};

  for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++)
  {
    if (*all[i] != NULL)
    {
      SDL_DestroyTexture(*all[i]);
      *all[i] = NULL;
    }
  }

  for (int i = 0; i < PAUSE_ITEM_COUNT; i++)
  {
    SDL_DestroyTexture(pauseItems[i]);
    pauseItems[i] = NULL;
  }
}
