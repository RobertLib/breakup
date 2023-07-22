#include "globals.h"
#include "types.h"
#include "embedded_assets.h"
#include <string.h>

SDL_Window *window;
SDL_Renderer *renderer;
MIX_Mixer *mixer;

TTF_Font *font16;
TTF_Font *font24;
TTF_Font *font32;
TTF_Font *font48;
TTF_Font *font64;

double dt;
double realDt;
float gameTime;

// Remaining hit stop, in real seconds.
static float hitstop;

bool isKeyDown[SDL_SCANCODE_COUNT];
bool isMouseButtonDown[8];

bool isKeyPressed[SDL_SCANCODE_COUNT];
bool isKeyRepeated[SDL_SCANCODE_COUNT];
bool isMousePressed[8];
bool anyKeyPressed;
bool mouseMoved;

int motionX, motionY, prevMotionX, prevMotionY;

bool isPause;
bool quitRequested;

const WorldTheme worldThemes[WORLD_COUNT + 1] = {
    // deep, accent, glow, name
    {{10, 16, 44, 255}, {30, 90, 150, 255}, {90, 200, 255, 255}, "CYAN DAWN"},
    {{8, 30, 24, 255}, {24, 110, 80, 255}, {110, 255, 170, 255}, "EMERALD DRIFT"},
    {{36, 14, 10, 255}, {130, 60, 24, 255}, {255, 170, 90, 255}, "EMBER FIELDS"},
    {{22, 10, 40, 255}, {90, 40, 140, 255}, {210, 130, 255, 255}, "VIOLET VOID"},
    {{12, 12, 34, 255}, {60, 50, 130, 255}, {140, 160, 255, 255}, "MENU"},
};

void addHitstop(float seconds)
{
  // The longest request wins rather than the sum, so that an explosion that
  // takes out a dozen bricks at once freezes the game once and briefly, instead
  // of a dozen times over and visibly.
  if (seconds > hitstop)
  {
    hitstop = seconds;
  }
}

void applyHitstop(void)
{
  if (hitstop <= 0)
  {
    dt = realDt;
    return;
  }

  // Counted down against the wall clock, which is the one thing here that a
  // frozen world cannot stop, so a hit stop can never leave the game stuck.
  hitstop -= (float)realDt;
  dt = 0;
}

void clearFrameInput(void)
{
  memset(isKeyPressed, 0, sizeof(isKeyPressed));
  memset(isKeyRepeated, 0, sizeof(isKeyRepeated));
  memset(isMousePressed, 0, sizeof(isMousePressed));
  anyKeyPressed = false;
  mouseMoved = false;
}

void updateTimer(Timer *timer)
{
  timer->elapsedTime += dt;

  if (timer->elapsedTime >= timer->duration)
  {
    if (timer->timeoutCallback)
    {
      timer->timeoutCallback(timer->userdata);
    }

    timer->elapsedTime = fmod(timer->elapsedTime, timer->duration);
  }
}

bool checkCollision(
    float aX, float aY, int aW, int aH,
    float bX, float bY, int bW, int bH)
{
  return (aX + aW > bX &&
          aX < bX + bW &&
          aY + aH > bY &&
          aY < bY + bH);
}

float lerp(float a, float b, float t)
{
  return (b - a) * t + a;
}

float easeOutBack(float t)
{
  const float c1 = 1.70158f;
  const float c3 = c1 + 1.0f;
  float u = t - 1.0f;
  return 1.0f + c3 * u * u * u + c1 * u * u;
}

float frand(void)
{
  // In double, because (float)RAND_MAX + 1.0f rounds to 2^31 and so does
  // (float)rand() for the largest few values, and the quotient was exactly
  // 1.0f - which the header promises never happens. The final test is for the
  // same rounding on the way back down to float.
  float r = (float)((double)rand() / ((double)RAND_MAX + 1.0));

  if (r >= 1.0f)
  {
    r = nextafterf(1.0f, 0.0f);
  }

  return r;
}

float frandRange(float a, float b)
{
  return a + frand() * (b - a);
}

SDL_FPoint getSize(SDL_Texture *texture)
{
  SDL_FPoint size = {0.0f, 0.0f};
  float w = 0.0f, h = 0.0f;

  if (texture == NULL || !SDL_GetTextureSize(texture, &w, &h))
  {
    // Once, not every frame: a texture that failed to load is measured by
    // whatever draws it sixty times a second.
    static bool reported;

    if (!reported)
    {
      fprintf(stderr, "getSize: %s\n",
              texture == NULL ? "no texture" : SDL_GetError());
      reported = true;
    }

    return size;
  }

  // Text textures carry a supersample factor; report their logical size
  float scale = (float)SDL_GetNumberProperty(
      SDL_GetTextureProperties(texture), "breakup.scale", 1);

  size.x = w / scale;
  size.y = h / scale;

  return size;
}

TTF_Font *loadFont(const char *file, float ptsize)
{
  TTF_Font *font = NULL;
  const EmbeddedAsset *asset = findEmbeddedAsset(file);

  // Rasterize at TEXT_SCALE x; text textures are drawn at logical size
  ptsize *= TEXT_SCALE;

  if (asset != NULL)
  {
    SDL_IOStream *io = SDL_IOFromConstMem(asset->data, asset->size);
    font = TTF_OpenFontIO(io, true, ptsize);
  }
  else
  {
    font = TTF_OpenFont(file, ptsize);
  }

  if (font == NULL)
  {
    fprintf(stderr, "Unable to load font %s! SDL_ttf Error: %s\n", file, SDL_GetError());
    exit(1);
  }

  return font;
}

static SDL_Texture *textureFromTextSurface(SDL_Surface *surface)
{
  if (surface == NULL)
  {
    fprintf(stderr, "Unable to render text surface! SDL_ttf Error: %s\n", SDL_GetError());
    exit(1);
  }

  SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);

  if (texture == NULL)
  {
    fprintf(stderr, "Unable to create texture from rendered text! SDL_ttf Error: %s\n", SDL_GetError());
    exit(1);
  }

  SDL_SetNumberProperty(SDL_GetTextureProperties(texture),
                        "breakup.scale", TEXT_SCALE);

  SDL_DestroySurface(surface);

  return texture;
}

// What actually gets handed to SDL_ttf.
//
// An empty string is not a blank texture: SDL_ttf refuses it outright, with
// "Text has zero width", and textureFromTextSurface() above turns that refusal
// into exit(1). So an empty string anywhere in the tree is not a missing label,
// it is the game closing on the player.
//
// Nothing reaches it today - every dynamic caller either formats a number or
// has a fallback, and parseLevelFromText() gives a nameless level the name
// "LEVEL" - but the ones that matter are fed from level files, and that is a
// documented mod point. A space rasterizes to a valid, and blank, texture, so
// every caller's assumptions survive: it gets a real texture back, getSize()
// answers, and nothing shows up on screen.
static const char *drawableText(const char *text)
{
  return (text == NULL || text[0] == '\0') ? " " : text;
}

SDL_Texture *renderTextBlended(TTF_Font *font, const char *text, SDL_Color fg)
{
  return textureFromTextSurface(
      TTF_RenderText_Blended(font, drawableText(text), 0, fg));
}

Vec2 vec2Norm(Vec2 vec, float m)
{
  float length = sqrtf(vec.x * vec.x + vec.y * vec.y);

  if (length != 0)
  {
    vec.x = (vec.x / length) * m;
    vec.y = (vec.y / length) * m;
  }

  return vec;
}
