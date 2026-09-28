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
#include "../lib/clip.h"
#include "../lib/effects.h"
#include "../lib/game-state.h"
#include "../lib/gfx.h"
#include "../lib/particles.h"
#include "../lib/postfx.h"
#include "../lib/save.h"
#include "../lib/starfield.h"
#include "../paddle/paddle.h"
#include "../run/run.h"
#include "../lib/vector.h"
#include "../story/pen.h"
#include "../story/presence.h"
#include "../story/story.h"
#include "../types.h"
#include "../ui/floating-text.h"
#include "../ui/run-overlay.h"
#include "../ui/status-bar.h"

#define PAUSE_ITEM_COUNT 3

// Where the pause menu's rows are drawn, which is also where the mouse finds
// them.
#define PAUSE_TOP 300.0f
#define PAUSE_STEP 44.0f

static SDL_Texture *getReadyText;
static SDL_Texture *levelBannerText;
static SDL_Texture *mutatorText; // a run's mutator and multiplier, under the banner
static SDL_Texture *levelCompleteText;
static SDL_Texture *bonusText;
static SDL_Texture *pausedText;
static SDL_Texture *clipHintText;
static SDL_Texture *pauseItems[PAUSE_ITEM_COUNT];

static int pauseSelection;
static int bannerLevel = -1;
static int bannerStage = -1;

// What a run's banner says under the level name: the mutator the stage drew,
// and what the score is being multiplied by once going deeper has raised it.
static void rebuildMutatorText(void)
{
  if (mutatorText != NULL)
  {
    SDL_DestroyTexture(mutatorText);
    mutatorText = NULL;
  }

  if (!runActive())
  {
    return;
  }

  char buf[128];
  Mutator mutator = runMutator();
  int curses = runCurseCount();

  if (mutator != MUTATOR_NONE && curses > 0)
  {
    snprintf(buf, sizeof(buf), "%s - %s\nSCORE X%.1f  -  %d CURSE%s",
             mutatorName(mutator), mutatorDescription(mutator),
             runScoreMultiplier(), curses, curses == 1 ? "" : "S");
  }
  else if (mutator != MUTATOR_NONE)
  {
    snprintf(buf, sizeof(buf), "%s - %s", mutatorName(mutator),
             mutatorDescription(mutator));
  }
  else if (curses > 0)
  {
    snprintf(buf, sizeof(buf), "SCORE X%.1f  -  %d CURSE%s",
             runScoreMultiplier(), curses, curses == 1 ? "" : "S");
  }
  else
  {
    return;
  }

  mutatorText = renderTextWrapped(font16, buf, (SDL_Color){255, 170, 90, 255},
                                  SCREEN_WIDTH - 60, true);
}

static void rebuildLevelBanner(void)
{
  if (levelBannerText != NULL)
  {
    SDL_DestroyTexture(levelBannerText);
  }

  int world = worldForLevel(paddle.level);
  const char *label = runIsDaily() ? "DAILY" : "ACT";
  char buf[80];

  if (runStageIsBoss())
  {
    snprintf(buf, sizeof(buf), "%s %d BOSS  -  %s", label, runAct() + 1,
             getLevel(paddle.level)->name);
  }
  else
  {
    snprintf(buf, sizeof(buf), "%s %d-%d  -  %s", label, runAct() + 1,
             runStageInAct() + 1, getLevel(paddle.level)->name);
  }

  levelBannerText = renderTextBlended(font32, buf, worldThemes[world].glow);
  bannerLevel = paddle.level;
  bannerStage = runActive() ? runStage() : -1;

  rebuildMutatorText();
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
  // Every game is a run. The menu and the run-over screen start one before
  // they come here; anything else that lands on this screen - a development
  // helper, say - gets a fresh one rather than a game with no run behind it.
  if (!runActive())
  {
    setStartLevel(startRun(false));
  }

  initializePaddle();
  initializeBalls();
  initializeBricks();
  initializeEnemies();
  initializeBoss();
  initializeCamera();
  initializeStatusBar();
  presenceBegin();

  resetEffects();
  resetBrickItems();
  clearParticles();
  clearFloatingTexts();

  isPause = false;
  pauseSelection = 0;
  bannerLevel = -1;
  bannerStage = -1;

  getReadyText = renderTextBlended(font24, "GET READY", (SDL_Color){255, 255, 255, 255});
  levelCompleteText = renderTextBlended(font48, "LEVEL COMPLETE!", (SDL_Color){255, 255, 255, 255});
  pausedText = renderTextBlended(font48, "PAUSED", (SDL_Color){255, 255, 255, 255});

  pauseItems[0] = renderTextBlended(font24, "RESUME", (SDL_Color){255, 255, 255, 255});
  // No restart: a level begun again is a perk draft and a mutator drawn again,
  // and the checkpoint that going deeper puts at risk would be one key away
  // from never being at risk at all.
  pauseItems[1] = renderTextBlended(font24, "END RUN", (SDL_Color){255, 255, 255, 255});
  pauseItems[2] = renderTextBlended(font24, "QUIT TO MENU", (SDL_Color){255, 255, 255, 255});

  rebuildLevelBanner();

#if BREAKUP_OFFERS_CLIPS
  clipHintText = renderTextBlended(font16, "G - SAVE THE LAST 8 SECONDS AS A GIF",
                                   (SDL_Color){110, 120, 160, 255});
#endif

  playMusic(musicForLevel(paddle.level));

  // After the paddle, because the prologue's map names the level it is on.
  storyRunBegan();

  // Development helper: BREAKUP_OVERLAY=1 opens a run's perk draft (or its
  // cash-out, on a boss stage) straight away, so a capture can photograph it
  // without playing a level to the end first.
  if (runActive() && SDL_getenv("BREAKUP_OVERLAY") != NULL)
  {
    openRunOverlay();
  }
}

static void activatePauseItem(int item)
{
  playSfx(SFX_MENU_SELECT);

  switch (item)
  {
  case 0:
    isPause = false;
    break;
  // Both of these end the run, and both record it: a score good enough for
  // the table is not thrown away by an exit that was not dying. Abandoning
  // costs what dying would, or neither would mean anything.
  case 1:
    isPause = false;
    runFinish(RUN_END_QUIT, paddle.score);
    nextGameState = GAME_STATE_RUN_OVER_SCREEN;
    break;
  case 2:
    isPause = false;
    runFinish(RUN_END_QUIT, paddle.score);
    nextGameState = GAME_STATE_MENU_SCREEN;
    break;
  }
}

// Pointing at a row selects it and clicking it takes it, with the same
// generous hit box the main menu gives its rows. The click that takes RESUME
// is spent here: the frame returns before the paddle reads it, so it does not
// also launch a ball.
static void handlePauseMouse(void)
{
  for (int i = 0; i < PAUSE_ITEM_COUNT; i++)
  {
    SDL_FPoint size = getSize(pauseItems[i]);
    float y = PAUSE_TOP + i * PAUSE_STEP;

    if (motionY < y - 8 || motionY > y + size.y + 8 ||
        motionX < SCREEN_WIDTH / 2 - 200 || motionX > SCREEN_WIDTH / 2 + 200)
    {
      continue;
    }

    if (mouseMoved && pauseSelection != i)
    {
      pauseSelection = i;
      playSfx(SFX_MENU_MOVE);
    }

    if (isMousePressed[1])
    {
      activatePauseItem(i);
    }

    return;
  }
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
    activatePauseItem(pauseSelection);
    return;
  }

  handlePauseMouse();
}

void updatePlaying(void)
{
  // G works whenever there is a game on screen, paused or not: the moment
  // worth keeping is usually the one that made somebody reach for the key.
  if (isKeyPressed[SDL_SCANCODE_G] && BREAKUP_OFFERS_CLIPS)
  {
    clipSave();
  }

  // The Sovereign speaking is all there is while it speaks: the level under it
  // waits exactly where it was, timers and all, and picks up when it is done.
  if (storyActive())
  {
    updateStory();
    return;
  }

  // Between two stages of a run the choice is all there is: the ball, the
  // paddle and the pause menu wait, and the field behind the cards goes on
  // settling the way it does under the level-complete banner.
  if (runOverlayActive())
  {
    updateRunOverlay();
    updatePresence();
    updateCamera();
    updateBricks();
    updateParticles();
    updateFloatingTexts();
    updateEffects();
    updateStatusBar();
    return;
  }

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

  if (bannerLevel != paddle.level || (runActive() ? runStage() : -1) != bannerStage)
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
    updatePresence();
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
  updatePresence();
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

// A rule either side of a banner, with the Sovereign's stylus at the inner end
// of each - the title card its scenes would give a stage.
static void drawBannerRules(SDL_Texture *tex, float y, SDL_Color color)
{
  SDL_FPoint size = getSize(tex);
  float maxW = SCREEN_WIDTH - 60;
  float scale = size.x > maxW ? maxW / size.x : 1.0f;
  float half = size.x * scale / 2.0f;
  float cy = y + size.y * scale / 2.0f;
  float left = SCREEN_WIDTH / 2.0f - half - 22.0f;
  float right = SCREEN_WIDTH / 2.0f + half + 22.0f;

  if (left < 60.0f)
  {
    return;
  }

  vecLine(24.0f, cy, left - 10.0f, cy, 1.0f, color, 0.8f);
  vecLine(right + 10.0f, cy, SCREEN_WIDTH - 24.0f, cy, 1.0f, color, 0.8f);
  drawStylus(left, cy, 6.0f, gameTime, color, 1.0f);
  drawStylus(right, cy, 6.0f, -gameTime, color, 1.0f);
  vecFlush();
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
  // A scene that covers the screen has nothing under it worth drawing. It
  // still goes through the scene pass, for the bloom its lines are drawn for.
  if (storyCoversScreen())
  {
    PostFx sceneFx = {.zoom = 1.0f, .bloom = 0.9f};
    bool scene = beginScene();

    drawStory();

    if (scene)
    {
      endScene(&sceneFx);
    }

    return;
  }

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

  drawBackground(worldForLevel(paddle.level), paddle.level, camera.y);
  drawPresence();

  drawBricks();
  drawBrickItems();
  drawBoss();
  drawPresenceOnCore();
  drawEnemies();
  drawBalls();
  drawPaddle();
  drawParticles();
  drawFloatingTexts();
  drawShieldBarrier();
  drawImpactFlash();

  camera.y = savedCameraY;

  // A scene fading in or out, over the field and inside the same pass, so the
  // two bloom as one picture. Nothing that is drawn after the pass - the HUD,
  // the banners - is drawn under a scene at all: it would sit on top of it.
  if (storyActive())
  {
    drawStory();
  }

  if (scene)
  {
    endScene(&fx);
  }

  if (storyActive())
  {
    return;
  }

  // Everything from here on is read rather than played, so it stays still and
  // stays sharp: outside the scene pass, nothing shakes it and nothing blooms
  // it into being harder to read.
  drawStatusBar();

  // The clip recorder keeps the field and the HUD over it, and nothing that
  // is drawn from here on: a pause menu or a perk draft is not the moment
  // anybody wants to show somebody.
  if (scene && !isPause && !runOverlayActive())
  {
    clipCaptureFrame(drawStatusBar);
  }

  if (!paddle.isReady && !isPause && !runOverlayActive())
  {
    drawOverlayBackdrop(110);
    drawCenteredTexture(levelBannerText, SCREEN_HEIGHT / 2.0f - 50);
    drawBannerRules(levelBannerText, SCREEN_HEIGHT / 2.0f - 50,
                    worldThemes[worldForLevel(paddle.level)].glow);

    if (fmodf(gameTime, 0.9f) < 0.62f)
    {
      drawCenteredTexture(getReadyText, SCREEN_HEIGHT / 2.0f + 14);
    }

    if (mutatorText != NULL)
    {
      drawCenteredTexture(mutatorText, SCREEN_HEIGHT / 2.0f + 54);
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
      float y = PAUSE_TOP + i * PAUSE_STEP;

      if (i == pauseSelection)
      {
        SDL_Color glow = {140, 230, 255, 255};
        float cy = y + size.y / 2;
        float offset = size.x / 2 + 24 + sinf(gameTime * 6.0f) * 2.0f;

        drawStylus(SCREEN_WIDTH / 2.0f - offset, cy, 7.0f, gameTime, glow, 1.0f);
        drawStylus(SCREEN_WIDTH / 2.0f + offset, cy, 7.0f, -gameTime, glow, 1.0f);
        vecFlush();
      }
      else
      {
        SDL_SetTextureColorMod(tex, 140, 150, 195);
      }

      SDL_FRect dst = {SCREEN_WIDTH / 2 - size.x / 2, y, size.x, size.y};
      SDL_RenderTexture(renderer, tex, NULL, &dst);
      SDL_SetTextureColorMod(tex, 255, 255, 255);
    }

    if (clipHintText != NULL)
    {
      drawCenteredTexture(clipHintText, SCREEN_HEIGHT - 60);
    }
  }

  drawRunOverlay();
}

void destroyPlaying(void)
{
  storyStop();
  destroyPaddle();
  destroyBalls();
  destroyBricks();
  destroyEnemies();
  destroyStatusBar();

  clearParticles();
  clearFloatingTexts();

  destroyRunOverlay();

  SDL_Texture **all[] = {
      &getReadyText, &levelBannerText, &mutatorText, &levelCompleteText,
      &bonusText, &pausedText, &clipHintText};

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
