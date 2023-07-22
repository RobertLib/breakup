#pragma once

// Platform detection
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_mixer/SDL_mixer.h>

#include <stdbool.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>

// The predicate is the *compiler*, not the operating system. This tested
// `_WIN32` and expanded to nothing there - but the Windows release is
// cross-built with mingw-w64, which is GCC and takes the attribute happily.
// So every `UNUSED` parameter in the tree lost its attribute in exactly the
// build that ships, and `make win` printed six -Wunused-parameter warnings
// that `make release` on this Mac had no way to show. The guard was presumably
// written for MSVC; MSVC does not define __GNUC__, so this still covers it.
#if defined(__GNUC__)
#define UNUSED __attribute__((__unused__))
#else
#define UNUSED
#endif

// Whether this build has a way out of itself to offer.
//
// Everywhere the game ships as a file somebody downloaded it has: QUIT is the
// last item of the menu and ESC on that page does the same thing. In a browser
// tab there is no process to close and nothing to be handed back to - the loop
// stops, the canvas stands there with the last frame frozen on it, and that
// reads as a crash rather than as something the player asked for. The tab is
// the way out and they already have it.
//
// A #define rather than an #ifdef at the two places that need one, because the
// item is also a count: the menu's arrow keys, its mouse hit tests and its
// texture cleanup all walk MAIN_ITEM_COUNT, and a platform test repeated at
// each of those is four places for them to disagree about how many rows there
// are. See src/screens/menu-screen.c.
#ifdef __EMSCRIPTEN__
#define BREAKUP_OFFERS_QUIT 0
#else
#define BREAKUP_OFFERS_QUIT 1
#endif

#define SCREEN_WIDTH 800
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
#define SCREEN_HEIGHT 1350
#else
#define SCREEN_HEIGHT 600
#endif
#define FPS 60

// The status bar is drawn over the top of the field rather than beside it, so
// the ceiling of the world is the bottom of that panel and not the top of the
// window.
//
// The ball, the enemies and the laser bolts all turn back here. They used to
// turn back at the top of the window, which put the bounce itself - and most
// of a 20px ball - behind a panel the player can only half see through. A
// bolt fired at the ceiling went out of sight before it expired, and a ball
// rallying off the top of a cleared level did it under the score.
//
// It is only reachable at all once the camera has bottomed out: above that,
// ballCameraPull() pins the ball at a quarter of the way down the screen and
// scrolls the world instead. So this is the top of the field as the player
// meets it at the top of a level, and LEVEL_PATTERN_TOP puts the first row of
// bricks a row below it.
//
// It is one number in one place because three files need to agree on it:
// level-types.h starts the brick pattern a row below it, src/ui/status-bar.c
// draws the panel that tall, and the three ceilings above measure from it.
#define STATUS_BAR_HEIGHT 46

// Text textures are rasterized at TEXT_SCALE x and drawn at logical size,
// so they stay sharp on high-DPI displays. getSize() returns the logical size.
#define TEXT_SCALE 2

typedef struct Vec2
{
  float x, y;
} Vec2;

typedef struct Obj
{
  Vec2 pos;
  Vec2 vel;
  int speed;
  bool active;
} Obj;

typedef struct Timer
{
  float elapsedTime;
  float duration;
  void (*timeoutCallback)(void *userdata);
  void *userdata;
} Timer;

void updateTimer(Timer *timer);

extern SDL_Window *window;
extern SDL_Renderer *renderer;
extern MIX_Mixer *mixer;

extern TTF_Font *font16;
extern TTF_Font *font24;
extern TTF_Font *font32;
extern TTF_Font *font48;
extern TTF_Font *font64;

// The world's clock and the wall clock. They are the same number except while
// a hit stop is running, and anything that should keep moving through an impact
// - the camera shake, a menu, the frame itself - reads realDt.
extern double dt;
extern double realDt;
extern float gameTime; // seconds since app start, for animations

// Freezes the world for `seconds` of real time without freezing the game: the
// frame goes on being drawn and input goes on being read, only dt stops. A few
// dozen milliseconds at the moment of an impact is the oldest trick there is
// for making one land - the eye gets a moment to register the hit before the
// world moves on, and it reads as weight rather than as a stutter.
void addHitstop(float seconds);

// Turns realDt into the dt the world runs on. main() calls it once a frame,
// before anything is updated.
void applyHitstop(void);

#define K_LEFT SDL_SCANCODE_LEFT
#define K_RIGHT SDL_SCANCODE_RIGHT
#define K_SPACE SDL_SCANCODE_SPACE
#define K_UP SDL_SCANCODE_UP
#define K_DOWN SDL_SCANCODE_DOWN
#define K_RETURN SDL_SCANCODE_RETURN
#define K_ESCAPE SDL_SCANCODE_ESCAPE

extern bool isKeyDown[SDL_SCANCODE_COUNT];
extern bool isMouseButtonDown[8];

// Edge-triggered input: true only on the frame the key/button went down
extern bool isKeyPressed[SDL_SCANCODE_COUNT];

// isKeyPressed, plus the keyboard's own auto-repeat while a key is held.
//
// Only the things that walk a list or slide a bar read this one - the menu
// arrows, the level select grid, the pause menu, the two volume sliders. Held
// down, an edge-triggered arrow does nothing at all, which made a ten-step
// volume control ten separate presses and a twenty-seven card grid a lot of
// tapping.
//
// Everything that commits to something stays on isKeyPressed, and the
// fullscreen row in the options is the reason it has to: repeating a toggle is
// the strobe that the SDLK_F handler in main.c carries its own paragraph about.
extern bool isKeyRepeated[SDL_SCANCODE_COUNT];

extern bool isMousePressed[8];
extern bool anyKeyPressed;
extern bool mouseMoved;

// Reset the per-frame (edge-triggered) input state; called once per frame
void clearFrameInput(void);

extern int motionX, motionY, prevMotionX, prevMotionY;

extern bool isPause;
extern bool quitRequested;

static inline int clampInt(int x, int a, int b)
{
  return x < a ? a : (x > b ? b : x);
}

// `value` brought back into [0, count), the way stepping round a list wants it
// - and unlike `%`, for a negative `value` as well.
//
// C's remainder keeps the sign of its left operand, so `(selection + count -
// step) % count` is the idiom that walks a list backwards and it is only
// correct while `step <= count`. The level select grid is where that stopped
// being true: its column count has a floor of five and the level files are a
// documented mod point, so a tree with two, three or four levels in it stepped
// UP to a selection of -1 or -2. Nothing indexed an array with it - the grid
// draws by walking `i` and comparing - so it showed as no card highlighted and
// ENTER starting level one whatever the player thought they had picked.
//
// It is here rather than in that screen because it is the same arithmetic the
// two menus do, and only its own count is compile-time. A screen whose list
// length is read off the disk should not have to re-derive when `%` is safe.
static inline int wrapIndex(int value, int count)
{
  if (count <= 0)
  {
    return 0;
  }

  int wrapped = value % count;

  return wrapped < 0 ? wrapped + count : wrapped;
}

// The two floating-point ones are written `x >= a ? ... : a` rather than
// `x < a ? a : ...`, and the difference is NaN.
//
// Every comparison against a NaN is false, so the obvious form returned the NaN
// itself: `x < a` was false, `x > b` was false, and out came the thing the
// clamp was called to rule out. The one place it mattered is the save file,
// which is plain text in the player's own directory and goes through
// `clamp(fv, 0.0f, 1.0f)` on its way in - `sfx=nan` reached setSfxVolume(),
// survived the `<= 0.001f` test in playSfxAt() that silences a muted game, and
// arrived at `(int)roundf(saveData.sfxVol * 10)` in the options screen, where
// converting a NaN to an int is undefined behaviour. Sound was off, the slider
// that would have turned it back on was drawn from the same cast, and the NaN
// was written back to the file on the way out - so it was permanent.
//
// Turning the test round makes the false answer fall to `a`, which is what a
// clamp should say about a number that is not one. Nothing else changes: for
// any ordinary x the two forms agree.
static inline float clampFloat(float x, float a, float b)
{
  return x >= a ? (x > b ? b : x) : a;
}

static inline double clampDouble(double x, double a, double b)
{
  return x >= a ? (x > b ? b : x) : a;
}

// Still a macro, because the arguments arrive as ints in one caller and floats
// in the next, and the game reads better for `clamp` meaning one thing. But it
// hands them to a function now, so each is evaluated exactly once.
//
// The plain expansion named `x` three times and `b` twice. Nothing in the tree
// passes it an argument with a side effect in it today - `clamp(SDL_atoi(env),
// 1, getNumberOfLevels())` merely did the work three times over - and this is
// the kind of trap that is cheaper to close than to remember.
#define clamp(x, a, b)             \
  _Generic((x) + (a) + (b),        \
      float: clampFloat,           \
      double: clampDouble,         \
      long double: clampDouble,    \
      default: clampInt)((x), (a), (b))

bool checkCollision(
    float aX, float aY, int aW, int aH,
    float bX, float bY, int bW, int bH);

float lerp(float a, float b, float t);

// Ease-out with a small overshoot bounce (t in [0, 1])
float easeOutBack(float t);

// Random float in [0, 1)
float frand(void);

// Random float in [a, b)
float frandRange(float a, float b);

TTF_Font *loadFont(const char *file, float ptsize);

SDL_FPoint getSize(SDL_Texture *texture);

// Anti-aliased text, rasterized at TEXT_SCALE x and drawn at logical size.
// Every label in the game goes through here; an empty string is turned into a
// space rather than handed to SDL_ttf, which refuses one outright.
SDL_Texture *renderTextBlended(TTF_Font *font, const char *text, SDL_Color fg);

Vec2 vec2Norm(Vec2 vec, float m);
