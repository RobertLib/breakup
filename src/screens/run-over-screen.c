#include "run-over-screen.h"
#include "../level-manager.h"
#include "../lib/audio.h"
#include "../lib/game-state.h"
#include "../lib/gfx.h"
#include "../lib/particles.h"
#include "../lib/postfx.h"
#include "../lib/save.h"
#include "../lib/starfield.h"
#include "../lib/transition.h"
#include "../paddle/paddle.h"
#include "../run/run.h"
#include "../lib/tunnel.h"
#include "../story/being.h"
#include "../story/script.h"
#include "../story/story.h"
#include "../types.h"

static SDL_Texture *headerText;
static SDL_Texture *dailyText;
static SDL_Texture *scoreText;
static SDL_Texture *lostText;
static SDL_Texture *reachedText;
static SDL_Texture *bestFlashText;
static SDL_Texture *bestText;
static SDL_Texture *infoText;

// The Sovereign stays for the result, as a ghost behind it: pleased with a
// death, put out by a run that got away with its points.
static Being face;
static Tunnel tunnel;
static SDL_Color tint;

static bool celebrate; // won or cashed out: fireworks rather than a dirge
static bool wasDaily;
static float fireworkTimer;

static void gotoMenu(void)
{
  nextGameState = GAME_STATE_MENU_SCREEN;
}

static void gotoNewRun(void)
{
  setStartLevel(startRun(wasDaily));
  storyRequestPrologue();
  nextGameState = GAME_STATE_PLAYING_SCREEN;
}

// The jingle, once the result is on the screen: straight away, or after the
// Sovereign has had its last word over it.
static void playJingle(void)
{
  playSfx(celebrate ? SFX_WIN : SFX_GAME_OVER);
}

void initializeRunOverScreen(void)
{
  const RunResult *result = runLastResult();

  // A run that beat The Sovereign won, however it ended afterwards: stopping
  // straight after it, cashing out somewhere in the endless acts, or dying in
  // them - the last only costs it the fireworks.
  bool won = result->won;
  bool stopped = result->end == RUN_END_WON || result->end == RUN_END_CASHED;

  celebrate = stopped;
  wasDaily = result->daily;
  fireworkTimer = 0.3f;

  stopMusic();
  clearParticles();

  int world = clamp(worldForLevel(paddle.level), 0, WORLD_COUNT - 1);

  tint = mixColor(worldThemes[world].glow,
                  stopped ? (SDL_Color){255, 220, 120, 255} : (SDL_Color){255, 90, 90, 255},
                  0.55f);

  beingReset(&face);
  beingPlace(&face, SCREEN_WIDTH / 2.0f, 300.0f);
  face.scale = 150.0f;
  face.eyeGlow = 3.0f;
  face.wander = 1.4f;
  beingSetMood(&face, won ? STORY_MOOD_SOFT : stopped ? STORY_MOOD_STERN : STORY_MOOD_AMUSED);
  beingSetGlitch(&face, storyDamage(result->act - 1, won));
  tunnelReset(&tunnel);

  const char *header = won && stopped ? "YOU WIN!"
                       : stopped      ? "CASHED OUT"
                                      : "RUN OVER";

  headerText = renderTextBlended(
      font64, header,
      stopped ? (SDL_Color){255, 220, 90, 255} : (SDL_Color){255, 90, 90, 255});

  char buf[96];
  char date[32] = "";

  if (result->daily)
  {
    int d = result->dailyDate;

    snprintf(date, sizeof(date), "DAILY RUN  %04d-%02d-%02d",
             d / 10000, d / 100 % 100, d % 100);
  }

  // One line under the header for the two things that can be said there.
  if (won || result->daily)
  {
    snprintf(buf, sizeof(buf), "%s%s%s", won ? "THE CORE IS SHATTERED" : "",
             won && result->daily ? "    " : "", date);
    dailyText = renderTextBlended(font16, buf, (SDL_Color){140, 235, 255, 255});
  }

  snprintf(buf, sizeof(buf), "SCORE  %06d", result->score);
  scoreText = renderTextBlended(font32, buf, (SDL_Color){255, 255, 255, 255});

  if (result->lost > 0)
  {
    snprintf(buf, sizeof(buf), "LOST %d - HALF SINCE YOU WENT DEEPER", result->lost);
    lostText = renderTextBlended(font16, buf, (SDL_Color){255, 130, 110, 255});
  }

  snprintf(buf, sizeof(buf), "REACHED ACT %d-%d  WITH %d PERK%s",
           result->act, result->stageInAct, result->perks,
           result->perks == 1 ? "" : "S");
  reachedText = renderTextBlended(font24, buf, (SDL_Color){190, 200, 230, 255});

  if (result->rank == 0)
  {
    snprintf(buf, sizeof(buf), "NEW BEST RUN!");
  }
  else if (result->rank > 0)
  {
    snprintf(buf, sizeof(buf), "NEW HIGH SCORE - #%d", result->rank + 1);
  }
  else
  {
    snprintf(buf, sizeof(buf), "NEW DAILY BEST!");
  }

  if (result->rank >= 0 || result->newDailyBest)
  {
    bestFlashText = renderTextBlended(font24, buf, (SDL_Color){255, 220, 90, 255});
  }

  if (saveData.highScores[0] > 0)
  {
    snprintf(buf, sizeof(buf), "BEST RUN  %06d  (ACT %d)",
             saveData.highScores[0], saveData.highActs[0]);
    bestText = renderTextBlended(font16, buf, (SDL_Color){150, 160, 200, 255});
  }

  infoText = renderTextBlended(
      font16,
      wasDaily ? "SPACE - MENU     R - PLAY THE DAILY AGAIN"
               : "SPACE - MENU     R - NEW RUN",
      (SDL_Color){150, 160, 200, 255});

  if (!storyPlayEpilogue(result, playJingle))
  {
    playJingle();
  }
}

void updateRunOverScreen(void)
{
  if (storyActive())
  {
    updateStory();
    return;
  }

  // Its eyes on the score.
  beingLookAt(&face, SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT / 2.0f - 50.0f);
  updateBeing(&face, (float)realDt);
  updateTunnel(&tunnel, (float)realDt);

  if (celebrate)
  {
    fireworkTimer -= (float)dt;

    if (fireworkTimer <= 0)
    {
      fireworkTimer = frandRange(0.45f, 0.9f);
      spawnFirework(frandRange(80, SCREEN_WIDTH - 80), frandRange(60, 260));
    }
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

  if (isKeyPressed[SDL_SCANCODE_R])
  {
    playSfx(SFX_MENU_SELECT);
    startTransition(gotoNewRun);
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

void drawRunOverScreen(void)
{
  PostFx fx = {.zoom = 1.0f, .bloom = celebrate ? 0.90f : 0.70f};
  bool scene = beginScene();

  if (storyCoversScreen())
  {
    drawStory();

    if (scene)
    {
      endScene(&fx);
    }

    return;
  }

  drawBackground(worldForLevel(paddle.level), paddle.level, gameTime * 12.0f);

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 6, 6, 18, 120);
  SDL_FRect full = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
  SDL_RenderFillRect(renderer, &full);

  drawTunnel(&tunnel, tint, 0.6f, SCREEN_WIDTH / 2.0f, 280.0f);
  drawBeing(&face, tint, 0.14f);

  drawParticles();

  renderCopyCenter(headerText, -160);
  renderCopyCenter(dailyText, -104);
  renderCopyCenter(scoreText, -60);
  renderCopyCenter(lostText, -20);
  renderCopyCenter(reachedText, 24);

  if (bestFlashText != NULL && fmodf(gameTime, 0.8f) < 0.55f)
  {
    renderCopyCenter(bestFlashText, 74);
  }

  renderCopyCenter(bestText, 118);
  renderCopyCenter(infoText, 178);

  drawStory();

  if (scene)
  {
    endScene(&fx);
  }
}

void destroyRunOverScreen(void)
{
  storyStop();
  clearParticles();

  SDL_Texture **all[] = {&headerText, &dailyText, &scoreText, &lostText,
                         &reachedText, &bestFlashText, &bestText, &infoText};

  for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++)
  {
    if (*all[i] != NULL)
    {
      SDL_DestroyTexture(*all[i]);
      *all[i] = NULL;
    }
  }
}
