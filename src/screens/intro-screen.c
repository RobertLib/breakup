#include "intro-screen.h"
#include "../lib/audio.h"
#include "../lib/game-state.h"
#include "../lib/gfx.h"
#include "../lib/postfx.h"
#include "../lib/starfield.h"
#include "../lib/transition.h"
#include "../types.h"

#define TITLE "BREAKUP"
#define TITLE_LEN ((int)(sizeof(TITLE) - 1))
#define LETTER_DELAY 0.13f
#define LETTER_DROP_TIME 0.55f

static SDL_Texture *letterTextures[TITLE_LEN];
static SDL_Texture *subtitleText;
static SDL_Texture *pressText;

static float timer;

static void changeGameState(void)
{
  nextGameState = GAME_STATE_MENU_SCREEN;
}

void initializeIntroScreen(void)
{
  timer = 0;

  const char title[] = TITLE;
  SDL_Color titleColor = {255, 255, 255, 255};

  for (int i = 0; i < TITLE_LEN; i++)
  {
    char letter[2] = {title[i], '\0'};
    letterTextures[i] = renderTextBlended(font64, letter, titleColor);
  }

  subtitleText = renderTextBlended(
      font16,
      "A  N E O N  A R C A D E  C L A S S I C",
      (SDL_Color){150, 170, 220, 255});

  pressText = renderTextBlended(
      font24,
      "PRESS ANY KEY",
      (SDL_Color){255, 255, 255, 255});

  playMusic(MUSIC_MENU);
}

void updateIntroScreen(void)
{
  timer += (float)dt;

  if ((anyKeyPressed || isMousePressed[1]) && !isTransitionActive())
  {
    if (timer < 1.6f)
    {
      // First press just skips the animation
      timer = 1.6f;
    }
    else
    {
      playSfx(SFX_MENU_SELECT);
      startTransition(changeGameState);
    }
  }
}

void drawIntroScreen(void)
{
  // Drawn through the same offscreen pass the field is, for the bloom and
  // for nothing else: there is nothing here to shake.
  PostFx fx = {.zoom = 1.0f, .bloom = 0.80f};
  bool scene = beginScene();

  drawBackground(WORLD_COUNT, gameTime * 24.0f);

  // Measure the title
  float totalWidth = 0;
  float letterWidths[TITLE_LEN];

  for (int i = 0; i < TITLE_LEN; i++)
  {
    letterWidths[i] = getSize(letterTextures[i]).x;
    totalWidth += letterWidths[i] + 6;
  }
  totalWidth -= 6;

  float x = SCREEN_WIDTH / 2.0f - totalWidth / 2.0f;
  float baseY = 200;

  SDL_Color glow = worldThemes[WORLD_COUNT].glow;

  for (int i = 0; i < TITLE_LEN; i++)
  {
    float t = (timer - i * LETTER_DELAY) / LETTER_DROP_TIME;

    if (t > 0)
    {
      float e = easeOutBack(fminf(1.0f, t));
      float y = baseY - (1.0f - e) * 300.0f;
      Uint8 alpha = (Uint8)(fminf(1.0f, t * 2.5f) * 255);

      float h = getSize(letterTextures[i]).y;

      // Neon glow behind the settled letter
      if (t >= 1.0f)
      {
        float pulse = 0.7f + 0.3f * sinf(gameTime * 2.4f + i * 0.6f);
        SDL_SetTextureColorMod(texGlow, glow.r, glow.g, glow.b);
        SDL_SetTextureAlphaMod(texGlow, (Uint8)(70 * pulse));
        SDL_FRect glowRect = {
            x + letterWidths[i] / 2 - 55,
            y + h / 2 - 55,
            110,
            110};
        SDL_RenderTexture(renderer, texGlow, NULL, &glowRect);
        SDL_SetTextureColorMod(texGlow, 255, 255, 255);
        SDL_SetTextureAlphaMod(texGlow, 255);
      }

      float wave = t >= 1.0f ? sinf(gameTime * 2.0f + i * 0.55f) * 4.0f : 0;

      SDL_SetTextureAlphaMod(letterTextures[i], alpha);
      SDL_FRect dst = {x, y + wave, letterWidths[i], h};
      SDL_RenderTexture(renderer, letterTextures[i], NULL, &dst);
      SDL_SetTextureAlphaMod(letterTextures[i], 255);
    }

    x += letterWidths[i] + 6;
  }

  // Subtitle
  if (timer > 1.7f)
  {
    Uint8 alpha = (Uint8)(fminf(1.0f, (timer - 1.7f) * 2.0f) * 255);
    SDL_FPoint size = getSize(subtitleText);
    SDL_SetTextureAlphaMod(subtitleText, alpha);
    SDL_FRect dst = {SCREEN_WIDTH / 2 - size.x / 2, 300, size.x, size.y};
    SDL_RenderTexture(renderer, subtitleText, NULL, &dst);
    SDL_SetTextureAlphaMod(subtitleText, 255);
  }

  // Blinking prompt
  if (timer > 2.2f && fmodf(timer, 1.1f) < 0.75f)
  {
    SDL_FPoint size = getSize(pressText);
    SDL_FRect dst = {SCREEN_WIDTH / 2 - size.x / 2, 430, size.x, size.y};
    SDL_RenderTexture(renderer, pressText, NULL, &dst);
  }

  if (scene)
  {
    endScene(&fx);
  }
}

void destroyIntroScreen(void)
{
  for (int i = 0; i < TITLE_LEN; i++)
  {
    SDL_DestroyTexture(letterTextures[i]);
    letterTextures[i] = NULL;
  }

  SDL_DestroyTexture(subtitleText);
  SDL_DestroyTexture(pressText);

  subtitleText = NULL;
  pressText = NULL;
}
