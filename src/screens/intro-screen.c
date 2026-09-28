#include "intro-screen.h"
#include "../lib/audio.h"
#include "../lib/game-state.h"
#include "../lib/gfx.h"
#include "../lib/postfx.h"
#include "../lib/starfield.h"
#include "../lib/transition.h"
#include "../lib/tunnel.h"
#include "../lib/vector.h"
#include "../story/being.h"
#include "../story/pen.h"
#include "../types.h"

// The title as the Sovereign writes it. The face scans itself in over the
// tunnel, and then the name of the game goes down under it a letter at a time,
// burnt in by the beam from its forehead - and spoken, three syllables of it,
// in the voice the rest of the game will hear it use. Then the line under the
// name, and then the prompt.

#define TITLE "BREAKUP"
#define TITLE_LEN ((int)(sizeof(TITLE) - 1))
#define LETTER_GAP 6.0f

#define FACE_Y 168.0f
#define FACE_SCALE 88.0f
#define TITLE_Y 314.0f
#define SUBTITLE_Y 404.0f
#define PROMPT_Y 470.0f

// When things happen, in seconds since the screen came up.
#define FIRST_LETTER 1.1f
#define LETTER_STEP 0.17f
#define SUBTITLE_AT (FIRST_LETTER + TITLE_LEN * LETTER_STEP + 0.35f)
#define SUBTITLE_TIME 1.0f
#define PROMPT_AT (SUBTITLE_AT + SUBTITLE_TIME + 0.25f)

static SDL_Texture *letterTextures[TITLE_LEN];
static float letterX[TITLE_LEN];
static float letterW[TITLE_LEN];
static float letterH;

static SDL_Texture *subtitleText;
static SDL_Texture *pressText;

static float timer;
static int written; // letters of the title down so far

static Being face;
static Pen pen;
static Tunnel tunnel;

static const SDL_Color white = {255, 255, 255, 255};

static void changeGameState(void)
{
  nextGameState = GAME_STATE_MENU_SCREEN;
}

static float letterBorn(int i)
{
  return FIRST_LETTER + i * LETTER_STEP;
}

static SDL_FPoint letterCentre(int i)
{
  return (SDL_FPoint){letterX[i] + letterW[i] / 2.0f, TITLE_Y + letterH / 2.0f};
}

void initializeIntroScreen(void)
{
  timer = 0;
  written = 0;

  const char title[] = TITLE;
  float total = 0;

  for (int i = 0; i < TITLE_LEN; i++)
  {
    char letter[2] = {title[i], '\0'};

    letterTextures[i] = renderTextBlended(font64, letter, white);
    letterW[i] = getSize(letterTextures[i]).x;
    letterH = getSize(letterTextures[i]).y;
    total += letterW[i] + LETTER_GAP;
  }

  float x = SCREEN_WIDTH / 2.0f - (total - LETTER_GAP) / 2.0f;

  for (int i = 0; i < TITLE_LEN; i++)
  {
    letterX[i] = x;
    x += letterW[i] + LETTER_GAP;
  }

  subtitleText = renderTextBlended(
      font16,
      "A  N E O N  A R C A D E  C L A S S I C",
      (SDL_Color){150, 170, 220, 255});

  pressText = renderTextBlended(
      font24,
      "PRESS ANY KEY",
      (SDL_Color){255, 255, 255, 255});

  beingReset(&face);
  beingPlace(&face, SCREEN_WIDTH / 2.0f, FACE_Y);
  face.scale = FACE_SCALE;
  beingSetMood(&face, STORY_MOOD_CALM);

  penReset(&pen, SCREEN_WIDTH / 2.0f, TITLE_Y);
  tunnelReset(&tunnel);

  playMusic(MUSIC_MENU);
}

// A letter goes down: sparks off it, and on the three that carry a syllable -
// BRE-A-KUP - the voice.
static void writeLetter(int i, bool quietly)
{
  SDL_FPoint at = letterCentre(i);

  for (int k = 0; k < 5; k++)
  {
    penSpark(&pen, at.x, at.y, 120.0f);
  }

  if (quietly)
  {
    return;
  }

  static const struct
  {
    int letter, vowel;
    float pitch;
  } syllables[] = {{0, 1, 0.0f}, {3, 0, 3.0f}, {5, 4, -2.0f}};

  for (size_t k = 0; k < SDL_arraysize(syllables); k++)
  {
    if (syllables[k].letter == i)
    {
      playVoice(syllables[k].vowel, syllables[k].pitch, 0.8f);
      beingSpeak(&face, 1.0f);
    }
  }
}

void updateIntroScreen(void)
{
  float step = (float)dt;

  timer += step;

  while (written < TITLE_LEN && timer >= letterBorn(written))
  {
    writeLetter(written, false);
    written++;
  }

  // The stylus goes where the next letter is, then along the line under the
  // name, and then back to the face.
  bool writing = true;
  SDL_FPoint want;

  if (written < TITLE_LEN && timer > FIRST_LETTER - 0.35f)
  {
    want = letterCentre(written);
    want.y -= letterH * 0.55f;
  }
  else if (timer >= SUBTITLE_AT && timer < SUBTITLE_AT + SUBTITLE_TIME)
  {
    SDL_FPoint size = getSize(subtitleText);
    float shown = (timer - SUBTITLE_AT) / SUBTITLE_TIME;

    want = (SDL_FPoint){SCREEN_WIDTH / 2.0f - size.x / 2.0f + size.x * shown,
                        SUBTITLE_Y - 6.0f};
  }
  else
  {
    writing = false;
    want = (SDL_FPoint){face.x + 110.0f, face.y + 30.0f};
  }

  penFollow(&pen, want.x, want.y, writing, step);
  updatePen(&pen, step);

  // It leans a little after its writing and watches it; with nothing left to
  // write it looks at whoever is there.
  beingSetTarget(&face, SCREEN_WIDTH / 2.0f + (pen.x - SCREEN_WIDTH / 2.0f) * 0.2f, FACE_Y);

  if (writing)
  {
    beingLookAt(&face, pen.x, pen.y);
  }
  else
  {
    beingLookAt(&face, face.x, face.y + 260.0f);
  }

  updateBeing(&face, step);
  updateTunnel(&tunnel, step);

  if ((anyKeyPressed || isMousePressed[1]) && !isTransitionActive())
  {
    if (timer < PROMPT_AT)
    {
      // First press only finishes the writing: the rest of the name goes down
      // at once, without the voice saying it over itself.
      while (written < TITLE_LEN)
      {
        writeLetter(written, true);
        written++;
      }

      timer = PROMPT_AT;
      beingSetVisible(&face, true);
      face.visible = 1.0f;
    }
    else
    {
      playSfx(SFX_MENU_SELECT);
      startTransition(changeGameState);
    }
  }
}

static void drawGlow(float x, float y, float w, float h, SDL_Color color, float alpha)
{
  SDL_SetTextureColorMod(texGlow, color.r, color.g, color.b);
  SDL_SetTextureAlphaMod(texGlow, (Uint8)(255 * clamp(alpha, 0.0f, 1.0f)));
  SDL_FRect dst = {x - w / 2, y - h / 2, w, h};
  SDL_RenderTexture(renderer, texGlow, NULL, &dst);
  SDL_SetTextureColorMod(texGlow, 255, 255, 255);
  SDL_SetTextureAlphaMod(texGlow, 255);
}

static void drawTitle(SDL_Color glow)
{
  for (int i = 0; i < written; i++)
  {
    float age = timer - letterBorn(i);
    float heat = expf(-age * 5.0f);
    float grow = 1.0f + 0.6f * expf(-age * 10.0f);
    SDL_FPoint at = letterCentre(i);

    // Settled, it floats on a slow wave, as the title always has.
    float wave = sinf(gameTime * 2.0f + i * 0.55f) * 4.0f * clamp(age - 0.4f, 0.0f, 1.0f);
    float pulse = 0.7f + 0.3f * sinf(gameTime * 2.4f + i * 0.6f);

    drawGlow(at.x, at.y + wave, 110, 110, glow, (70.0f * pulse + 180.0f * heat) / 255.0f);

    SDL_Color c = mixColor(white, glow, 0.15f * (1.0f - heat));
    float w = letterW[i] * grow;
    float h = letterH * grow;
    SDL_FRect dst = {at.x - w / 2, at.y + wave - h / 2, w, h};

    SDL_SetTextureColorMod(letterTextures[i], c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(letterTextures[i], (Uint8)(255 * clamp(age * 12.0f, 0.0f, 1.0f)));
    SDL_RenderTexture(renderer, letterTextures[i], NULL, &dst);
    SDL_SetTextureColorMod(letterTextures[i], 255, 255, 255);
    SDL_SetTextureAlphaMod(letterTextures[i], 255);
  }
}

// The line under the name, uncovered from the left as the stylus goes along it.
static void drawSubtitle(void)
{
  float shown = clamp((timer - SUBTITLE_AT) / SUBTITLE_TIME, 0.0f, 1.0f);

  if (shown <= 0)
  {
    return;
  }

  SDL_FPoint size = getSize(subtitleText);
  float texW, texH;

  SDL_GetTextureSize(subtitleText, &texW, &texH);

  SDL_FRect src = {0, 0, texW * shown, texH};
  SDL_FRect dst = {SCREEN_WIDTH / 2.0f - size.x / 2.0f, SUBTITLE_Y, size.x * shown, size.y};

  SDL_RenderTexture(renderer, subtitleText, &src, &dst);
}

void drawIntroScreen(void)
{
  // Drawn through the same offscreen pass the field is, for the bloom and
  // for nothing else: there is nothing here to shake.
  PostFx fx = {.zoom = 1.0f, .bloom = 0.85f};
  bool scene = beginScene();

  SDL_Color glow = worldThemes[WORLD_COUNT].glow;

  // The menu's own sky, sunk most of the way into the dark the Sovereign's
  // scenes are played in, and its tunnel over that.
  drawBackground(WORLD_COUNT, BACKGROUND_MENU, gameTime * 24.0f);

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 6, 6, 18, 150);
  SDL_FRect full = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
  SDL_RenderFillRect(renderer, &full);

  drawGlow(face.x, face.y, 600, 460, glow, 0.16f);
  drawTunnel(&tunnel, glow, 0.9f, SCREEN_WIDTH / 2.0f + (face.x - SCREEN_WIDTH / 2.0f) * 0.25f,
             200.0f);

  drawBeing(&face, glow, 0.85f);
  drawPen(&pen, pen.presence > 0.01f ? &face.penOrigin : NULL, glow, 1.0f);

  drawTitle(glow);
  drawSubtitle();

  // Blinking prompt
  if (timer > PROMPT_AT && fmodf(timer - PROMPT_AT, 1.1f) < 0.75f)
  {
    SDL_FPoint size = getSize(pressText);
    SDL_FRect dst = {SCREEN_WIDTH / 2 - size.x / 2, PROMPT_Y, size.x, size.y};
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
