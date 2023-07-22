#include "globals.h"
#include "lib/audio.h"
#include "lib/camera.h"
#include "lib/game-state.h"
#include "lib/gfx.h"
#include "lib/postfx.h"
#include "lib/save.h"
#include "lib/starfield.h"
#include "lib/transition.h"
#include "paddle/paddle.h"
#include "ui/floating-text.h"
#include "level-manager.h"
#include "version.h"
#include <time.h>

#ifdef __EMSCRIPTEN__
// Two headers, and neither of them is here for the loop alone.
//
// <emscripten.h> is emscripten_set_main_loop, at the bottom of main().
//
// <SDL3/SDL_main.h> is the save file. It renames this main() to SDL_main and
// provides an entry point of SDL's own, and what that entry point does before
// calling this one is mount the browser's IndexedDB over the prefix path and
// wait for it to load - `FS.mount(IDBFS, { autoPersist: true })` and an
// `FS.syncfs()`, in SDL's src/main/emscripten/SDL_sysmain_runapp.c, switched on
// by the SDL_EMSCRIPTEN_PERSISTENT_PATH that packaging/build_web.sh compiles SDL
// with. A program that keeps its own main() never goes through any of it: a
// browser tab's filesystem is RAM, so SDL_GetPrefPath() hands back a directory
// that works, save.c writes into it and succeeds, and every setting, unlocked
// level and high score is gone the moment the tab closes. Silently, because
// nothing failed.
//
// It is included on this platform only. On the other three SDL_main.h is either
// unnecessary or a change to how the program starts - it is what puts a WinMain
// in front of a Windows build - and none of them has anything to gain from it.
#include <SDL3/SDL_main.h>
#include <emscripten.h>
#endif

// The filename for one frame of a capture: the path as given when there is only
// one frame, and `path-0000.bmp` for a burst, so that a burst assembles in
// filename order whatever puts it together.
static void shotFilename(char *out, size_t size, const char *base, int index,
                         int total)
{
  if (total <= 1)
  {
    SDL_strlcpy(out, base, size);
    return;
  }

  const char *dot = SDL_strrchr(base, '.');
  int stem = dot != NULL ? (int)(dot - base) : (int)SDL_strlen(base);

  snprintf(out, size, "%.*s-%04d%s", stem, base, index,
           dot != NULL ? dot : ".bmp");
}

// One frame, written as a BMP, cropped to the game's own frame.
//
// SDL_RenderReadPixels reads the pixels on the screen, and under letterbox
// presentation those are not the pixels the game drew into: it is the whole
// window, black bars included, at whatever scale that window imposes on the
// logical 800x600. This read back 1024x768 on a machine whose save had
// `fullscreen=1` in it, so every screenshot ever taken here arrived as pixel art
// scaled by 1.28 before anything downstream had touched it.
// SDL_GetRenderLogicalPresentationRect is where the content actually landed -
// 800x600 exactly in a window that size, a whole multiple of it on a
// high-density display, and no bar in either case.
static bool saveShot(const char *path, int frame)
{
  SDL_FRect area;
  SDL_Rect crop;
  const SDL_Rect *rect = NULL;

  if (SDL_GetRenderLogicalPresentationRect(renderer, &area))
  {
    crop.x = (int)SDL_lroundf(area.x);
    crop.y = (int)SDL_lroundf(area.y);
    crop.w = (int)SDL_lroundf(area.w);
    crop.h = (int)SDL_lroundf(area.h);
    rect = &crop;
  }

  SDL_Surface *shot = SDL_RenderReadPixels(renderer, rect);

  if (shot == NULL)
  {
    fprintf(stderr, "Could not read frame %d: %s\n", frame, SDL_GetError());
    return false;
  }

  bool written = SDL_SaveBMP(shot, path);

  if (!written)
  {
    fprintf(stderr, "Could not write %s: %s\n", path, SDL_GetError());
  }
  else
  {
    // One line per file, and it names the size, because a capture whose frame
    // came out at the wrong size is the failure this whole path is about.
    printf("Wrote %s (%dx%d) at frame %d\n", path, shot->w, shot->h, frame);
  }

  SDL_DestroySurface(shot);

  return written;
}

// ---------------------------------------------------------------------------
// One frame, and the state that has to live between two of them.
//
// This was all local to main(), inside a `while` loop, and on three of the four
// platforms it still reads that way. The browser is the fourth and it does not
// own its own loop: a page that spins in C never yields to the event loop that
// draws it, so the canvas stays blank, the tab stops answering and Chrome
// offers to kill it. `emscripten_set_main_loop` is the other way round - the
// browser calls one function per repaint - which means every local the loop
// carried across an iteration has to outlive the call, and that is the whole
// reason these are at file scope.
//
// Written down here rather than in a struct passed through a void*: there is
// exactly one game in a process, main() sets them up and runFrame() reads them,
// and a pointer would buy nothing but a cast at each end.
// ---------------------------------------------------------------------------

static bool scripted;

static int shotFrame = -1;
static int shotFrames = 1;
static int shotStep = 1;
static char shotPath[512];

static int keyFrames[32];
static int keyCodes[32];
static int numKeyEvents;

static int frameCounter;
static int shotsTaken;
static bool running = true;
static Uint64 lastTime;

static void runFrame(void)
{
  Uint64 startTime;
  SDL_Event event;

  // Calculate delta time (clamped to avoid tunneling after hitches)
  startTime = SDL_GetTicks();
  realDt = (startTime - lastTime) / 1000.f;
  lastTime = startTime;

  if (realDt > 1.0 / 30.0)
  {
    realDt = 1.0 / 30.0;
  }

  // A scripted run steps the world instead of watching the clock, so that the
  // same frame number is the same moment in the game on any machine and at any
  // speed. Without it a capture is a photograph of how fast the machine that
  // took it happened to be.
  if (scripted)
  {
    realDt = 1.0 / FPS;
  }

  // dt is realDt unless an impact has just frozen the world for a few
  // hundredths of a second. gameTime is not: it paces the blinking prompts
  // and the pulsing barrier, and those belong to the screen rather than to
  // the world, so they go on running through a hit stop.
  applyHitstop();

  gameTime += (float)realDt;

  // Input
  while (SDL_PollEvent(&event))
  {
    // Map window coordinates to the logical 800x600 space
    SDL_ConvertEventToRenderCoordinates(renderer, &event);

    switch (event.type)
    {
    case SDL_EVENT_QUIT:
      running = false;
      break;
    case SDL_EVENT_KEY_DOWN:
      isKeyDown[event.key.scancode] = true;

      // Outside the repeat guard on purpose: this is the one input a held key
      // is allowed to keep producing. See isKeyRepeated in globals.h.
      isKeyRepeated[event.key.scancode] = true;

      if (!event.key.repeat)
      {
        isKeyPressed[event.key.scancode] = true;

        // F toggles fullscreen; it should not also act as "any key"
        if (event.key.key != SDLK_F)
        {
          anyKeyPressed = true;
        }

        // Inside the repeat guard, and that is the whole of this. Outside it,
        // holding F down did not toggle the window once: it toggled on every
        // auto-repeat the keyboard sent, about thirty a second, strobing
        // between fullscreen and windowed - each one an animated transition on
        // macOS - and writing the save file to disk every time round.
        if (event.key.key == SDLK_F)
        {
#if !defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE
          saveData.fullscreen = !saveData.fullscreen;
          SDL_SetWindowFullscreen(window, saveData.fullscreen);
          writeSave();
#endif
        }
      }
      break;
    case SDL_EVENT_KEY_UP:
      isKeyDown[event.key.scancode] = false;
      break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
      if (event.button.button < 8)
      {
        isMouseButtonDown[event.button.button] = true;
        isMousePressed[event.button.button] = true;
      }
      break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
      if (event.button.button < 8)
      {
        isMouseButtonDown[event.button.button] = false;
      }
      break;
    case SDL_EVENT_MOUSE_MOTION:
      motionX = (int)event.motion.x;
      motionY = (int)event.motion.y;
      mouseMoved = true;
      break;
    }
  }

  // Inject scripted key presses (development helper)
  for (int i = 0; i < numKeyEvents; i++)
  {
    if (keyFrames[i] == frameCounter)
    {
      isKeyPressed[keyCodes[i]] = true;
      isKeyRepeated[keyCodes[i]] = true;
      anyKeyPressed = true;
    }
  }

  // Update
  updateGameState();
  updateTransition();

  // Draw
  SDL_SetRenderDrawColor(renderer, 0x00, 0x00, 0x00, 0xff);

  SDL_RenderClear(renderer);

  drawGameState();
  drawVignette();
  drawTransition();

  if (scripted && frameCounter >= shotFrame && shotsTaken < shotFrames &&
      (frameCounter - shotFrame) % shotStep == 0)
  {
    char path[576];

    shotFilename(path, sizeof(path), shotPath, shotsTaken, shotFrames);
    saveShot(path, frameCounter);

    shotsTaken++;

    if (shotsTaken >= shotFrames)
    {
      quitRequested = true;
    }
  }

  frameCounter++;

  SDL_RenderPresent(renderer);

  // Cap the frame rate (fallback when VSync is unavailable). A scripted run
  // wants no part of this: it is stepping the world by a fixed amount, so
  // waiting for the clock to catch up buys it nothing but wall time.
  //
  // Neither does a browser, and there the cap is not merely useless but harmful:
  // the frame is already paced by requestAnimationFrame, and SDL_Delay is the
  // tab's one thread standing still - it is not this game going quiet for a few
  // milliseconds, it is the whole page. See emscripten_set_main_loop in main().
#ifndef __EMSCRIPTEN__
  if (!scripted)
  {
    Uint64 elapsed = SDL_GetTicks() - startTime;
    float targetTime = 1000.f / FPS;
    if (targetTime > (float)elapsed)
    {
      SDL_Delay((Uint32)(targetTime - (float)elapsed));
    }
  }
#endif

  // Update previous motion
  prevMotionX = motionX;
  prevMotionY = motionY;

  // Update previous camera position
  camera.prevY = camera.y;

  clearFrameInput();

  paddleNextLevelUpdate();
  changeGameStateUpdate();
}

// Everything the run leaves behind, in the order it was built. Called once, off
// the end of the loop - and on the web off the end of a frame that asked to
// stop, which is why it is a function rather than the tail of main().
static void shutdownGame(void)
{
  // A run that ends by closing the window is still a run. recordScore() lived
  // on the game over and win screens only, so the one exit from the game that
  // does not pass through either of them - the close button, mid-level, which
  // is how a session usually ends - threw the score away.
  if (getGameState() == GAME_STATE_PLAYING_SCREEN)
  {
    recordScore(paddle.score, paddle.level + 1);
  }

  // Cleanup
  writeSave();

  destroyGameState();
  destroyTransition();
  destroyLevelManager();
  destroyAudio();
  destroyFloatingTexts();
  destroyPostFx();
  destroyGfx();

  TTF_CloseFont(font16);
  TTF_CloseFont(font24);
  TTF_CloseFont(font32);
  TTF_CloseFont(font48);
  TTF_CloseFont(font64);

  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);

  if (mixer != NULL)
  {
    MIX_DestroyMixer(mixer);
  }
  MIX_Quit();

  TTF_Quit();
  SDL_Quit();
}

#ifdef __EMSCRIPTEN__
// The browser's call, once per repaint. It is the loop condition that used to
// sit at the top of the `while`, in the only place it can go when somebody else
// owns the loop: after the frame, because a frame that has just set
// `quitRequested` is a frame that has already been drawn.
//
// Nothing here is reachable in the build that ships - the browser is handed no
// way to quit (see BREAKUP_OFFERS_QUIT in globals.h) and a closing tab raises no
// SDL_EVENT_QUIT. It is written anyway because a loop with no way out of it is
// the kind of thing that stops being true one commit later, and because
// cancelling the loop without the writeSave() inside shutdownGame() would be a
// player's progress thrown away by the one path nobody would have played.
static void webFrame(void)
{
  runFrame();

  if (!running || quitRequested)
  {
    shutdownGame();
    emscripten_cancel_main_loop();
  }
}
#endif

// The arguments are taken and ignored, on every platform, and that is not an
// oversight kept for symmetry: on the web this function is SDL_main (see the
// SDL_main.h note at the top), SDL's own entry point calls it through an
// `int (*)(int, char **)`, and a wasm indirect call whose signature does not
// match traps at the call rather than warning at the compile. Nothing here has
// ever read a command line - every development helper arrives as an environment
// variable - so the parameters exist to be the right shape and nothing else.
int main(UNUSED int argc, UNUSED char *argv[])
{
  // Development helpers, all read here at the top because BREAKUP_SHOT decides
  // how the rest of this function behaves and everything it changes is set up
  // before the main loop:
  //
  //   BREAKUP_STATE=intro|menu|levels|playing|gameover|win  jump to a state
  //   BREAKUP_LEVEL=N   start playing at level N (1-based)
  //   BREAKUP_SCORE=N   start with N points
  //   BREAKUP_UNLOCKED=N   pretend N levels have been unlocked
  //   BREAKUP_KEYS=frame:scancode,frame:scancode  inject key presses
  //   BREAKUP_AUTOPLAY=1   the paddle tracks the ball (see playing-screen.c)
  //   BREAKUP_SEED=N    seed the RNG, instead of the clock
  //   BREAKUP_SHOT=frame:path.bmp   save a screenshot at that frame and quit
  //   BREAKUP_SHOT_FRAMES=N   write N frames from there on, path-0000.bmp up
  //   BREAKUP_SHOT_STEP=K     keep every K-th frame of that burst
  const char *stateEnv = SDL_getenv("BREAKUP_STATE");
  const char *levelEnv = SDL_getenv("BREAKUP_LEVEL");
  const char *scoreEnv = SDL_getenv("BREAKUP_SCORE");
  const char *unlockedEnv = SDL_getenv("BREAKUP_UNLOCKED");
  const char *keysEnv = SDL_getenv("BREAKUP_KEYS");
  const char *seedEnv = SDL_getenv("BREAKUP_SEED");
  const char *shotEnv = SDL_getenv("BREAKUP_SHOT");
  const char *shotFramesEnv = SDL_getenv("BREAKUP_SHOT_FRAMES");
  const char *shotStepEnv = SDL_getenv("BREAKUP_SHOT_STEP");

  // Seeded on request, so that two captures of one commit are two copies of the
  // same picture: the starfields, the particles, the power-up drops and the
  // enemies all come out of this. The clock stays the default - a player wants a
  // different night every time, and only a measurement wants the same one twice.
  srand(seedEnv != NULL ? (unsigned int)SDL_atoi(seedEnv)
                        : (unsigned int)time(NULL));

  if (shotEnv != NULL)
  {
    const char *colon = SDL_strchr(shotEnv, ':');

    if (colon != NULL)
    {
      shotFrame = SDL_atoi(shotEnv);
      SDL_strlcpy(shotPath, colon + 1, sizeof(shotPath));
    }
    else
    {
      fprintf(stderr, "Could not read BREAKUP_SHOT=%s; it wants frame:path.bmp\n",
              shotEnv);
    }
  }

  // A capture is a *scripted* run, and that is three things beyond writing a
  // file. It reads and writes no save, so it plays the game as it ships rather
  // than as this machine has it - which matters most for the fullscreen flag,
  // because that is what decides the size every captured frame comes out at, and
  // it means a press run cannot overwrite anybody's progress or high scores. It
  // advances the world by a fixed step rather than by the wall clock, so a frame
  // number is a moment in the game instead of a moment on the machine. And it
  // paces itself to nothing at all - neither the frame cap at the bottom of the
  // loop nor vsync - so a shot deep into a level costs a fraction of the time
  // that level takes to play.
  scripted = shotFrame >= 0;

  if (scripted)
  {
    if (shotFramesEnv != NULL)
    {
      shotFrames = SDL_max(1, SDL_atoi(shotFramesEnv));
    }

    if (shotStepEnv != NULL)
    {
      shotStep = SDL_max(1, SDL_atoi(shotStepEnv));
    }
  }

  setSaveScripted(scripted);

  // Init SDL, SDL_ttf, SDL_mixer
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
  {
    fprintf(stderr, "SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
    exit(1);
  }

  if (!TTF_Init())
  {
    fprintf(stderr, "SDL_ttf could not initialize! SDL_ttf Error: %s\n", SDL_GetError());
    exit(1);
  }

  // Create window
  // High pixel density keeps rendering sharp on Retina/HiDPI displays
  window = SDL_CreateWindow(
      BREAKUP_APP_NAME,
      SCREEN_WIDTH,
      SCREEN_HEIGHT,
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
      SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_FULLSCREEN);
#else
      SDL_WINDOW_HIGH_PIXEL_DENSITY);
#endif

  if (window == NULL)
  {
    fprintf(stderr, "Window could not be created! SDL_Error: %s\n", SDL_GetError());
    exit(1);
  }

  // Create renderer
  renderer = SDL_CreateRenderer(window, NULL);

  if (renderer == NULL)
  {
    fprintf(stderr, "Renderer could not be created! SDL Error: %s\n", SDL_GetError());
    exit(1);
  }

  // Enable vsync - except in a scripted run, which is stepping the world by a
  // fixed amount and writing the frames to a disk, and has nothing to gain by
  // waiting for a display nobody is looking at. This, rather than the frame cap
  // at the bottom of the loop, is what was pacing a capture: with the cap
  // skipped and vsync left on, twenty seconds of game still cost twenty seconds
  // of waiting, because the wait had simply moved into SDL_RenderPresent.
  SDL_SetRenderVSync(renderer, scripted ? 0 : 1);

  // Set logical size
  SDL_SetRenderLogicalPresentation(renderer, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_LOGICAL_PRESENTATION_LETTERBOX);

  // Initialize audio mixer (the game keeps running without audio if this fails)
  if (MIX_Init())
  {
    mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);

    if (mixer == NULL)
    {
      fprintf(stderr, "SDL_mixer could not create mixer! SDL Error: %s\n", SDL_GetError());
    }
  }
  else
  {
    fprintf(stderr, "SDL_mixer could not initialize! SDL Error: %s\n", SDL_GetError());
  }

  // Load fonts
  font16 = loadFont("assets/font.ttf", 16);
  font24 = loadFont("assets/font.ttf", 24);
  font32 = loadFont("assets/font.ttf", 32);
  font48 = loadFont("assets/font.ttf", 48);
  font64 = loadFont("assets/font.ttf", 64);

  // Initialization.
  //
  // The levels load *before* the save, and the order is the point: loadSave()
  // clamps `unlocked` to how many levels there actually are, and with it the
  // other way round it had no count to clamp against and used a hard-coded 100
  // instead. Nothing in the level manager reads saveData, so this costs
  // nothing.
  initializeLevelManager();
  loadSave();
  initializeGfx();
  initializePostFx();
  initializeAudio();
  initializeStarfield();
  initializeGameState();
  initializeTransition();

  setMusicVolume(saveData.musicVol);
  setSfxVolume(saveData.sfxVol);

  if (saveData.fullscreen)
  {
    SDL_SetWindowFullscreen(window, true);
  }

  // Not addScore(), which is what this was: initializePlaying() runs later, at
  // the bottom of the first frame, and initializePaddle() sets the score to
  // zero on its way past - so BREAKUP_SCORE was silently ignored by every
  // capture of the one screen that shows a score being played for.
  // setStartScore() both sets it now (for =gameover and =win, which never
  // initialize a paddle) and survives the initializePaddle() that follows.
  if (scoreEnv != NULL)
  {
    setStartScore(SDL_atoi(scoreEnv));
  }

  // Only the level select screen reads this, and it is the one screen a capture
  // cannot photograph honestly without it: a scripted run starts from the
  // shipped save, where exactly one level is unlocked, so the grid is twenty-six
  // padlocks and no level names. It is not written to any save file.
  //
  // Both of the clamps below go the wrong way round when there are no levels
  // at all: clamp(n, 1, 0) is 0 and clamp(n, 0, -1) is -1, and -1 is what
  // setStartLevel() would then hand to the bricks. A build whose assets did
  // not embed is a broken build, but it should say so rather than index
  // backwards off an array.
  const bool haveLevels = getNumberOfLevels() > 0;

  if (!haveLevels && (unlockedEnv != NULL || levelEnv != NULL))
  {
    fprintf(stderr, "No levels loaded; BREAKUP_LEVEL and BREAKUP_UNLOCKED ignored\n");
  }

  if (haveLevels && unlockedEnv != NULL)
  {
    saveData.unlocked = clamp(SDL_atoi(unlockedEnv), 1, getNumberOfLevels());
  }

  if (haveLevels && levelEnv != NULL)
  {
    setStartLevel(clamp(SDL_atoi(levelEnv) - 1, 0, getNumberOfLevels() - 1));
    nextGameState = GAME_STATE_PLAYING_SCREEN;
  }
  else if (stateEnv != NULL)
  {
    if (SDL_strcmp(stateEnv, "intro") == 0)
      nextGameState = GAME_STATE_INTRO_SCREEN;
    else if (SDL_strcmp(stateEnv, "menu") == 0)
      nextGameState = GAME_STATE_MENU_SCREEN;
    else if (SDL_strcmp(stateEnv, "levels") == 0)
      nextGameState = GAME_STATE_LEVEL_SELECT_SCREEN;
    else if (SDL_strcmp(stateEnv, "playing") == 0)
      nextGameState = GAME_STATE_PLAYING_SCREEN;
    else if (SDL_strcmp(stateEnv, "gameover") == 0)
      nextGameState = GAME_STATE_GAME_OVER_SCREEN;
    else if (SDL_strcmp(stateEnv, "win") == 0)
      nextGameState = GAME_STATE_WIN_SCREEN;
  }

  if (keysEnv != NULL)
  {
    const char *p = keysEnv;
    while (*p != '\0' && numKeyEvents < 32)
    {
      keyFrames[numKeyEvents] = SDL_atoi(p);
      const char *colon = SDL_strchr(p, ':');
      if (colon == NULL)
      {
        break;
      }
      int scancode = SDL_atoi(colon + 1);

      // isKeyPressed is an array of SDL_SCANCODE_COUNT bools and this number
      // comes off the command line, so it is checked here rather than trusted
      // at the point it is used to index one.
      if (scancode < 0 || scancode >= SDL_SCANCODE_COUNT)
      {
        fprintf(stderr, "BREAKUP_KEYS: %d is not a scancode (0..%d); ignored\n",
                scancode, SDL_SCANCODE_COUNT - 1);
      }
      else
      {
        keyCodes[numKeyEvents] = scancode;
        numKeyEvents++;
      }

      const char *comma = SDL_strchr(p, ',');
      if (comma == NULL)
      {
        break;
      }
      p = comma + 1;
    }
  }


  // The clock the first frame's delta is measured from. It is read here rather
  // than at the top of main() because everything above it - three libraries, a
  // window, a renderer, nineteen Ogg Vorbis tracks handed to SDL_mixer and five
  // font sizes rasterized - is most of a second, and a first frame that begins
  // by advancing the world through it is a ball that has already moved before
  // anybody has seen it. (It is clamped to 1/30 either way; this is the
  // difference between the clamp firing on frame one and it never firing.)
  lastTime = SDL_GetTicks();

#ifdef __EMSCRIPTEN__
  // The browser owns the loop. The 0 asks for requestAnimationFrame, which is
  // the display's own rate rather than a number this program picked - so the
  // frame cap at the bottom of runFrame() is skipped on this platform, and has
  // to be: SDL_Delay on the web is the tab's only thread sitting still, and a
  // player would feel it as the whole page going stiff rather than as the game
  // being paced.
  //
  // The last argument is `simulate_infinite_loop` and it is nought rather than
  // SDL's suggested 1, which is worth the paragraph.
  //
  // At 1, emscripten does not return from this call: it throws a Javascript
  // `unwind` to get off the stack, on the grounds that whatever sits below the
  // loop in a main() written for a desktop should not run. There is nothing
  // below this one but `return 0`, and the throw has somewhere to land only
  // when main was called from emscripten's own runtime. It is not: SDL's
  // entry point calls this function from inside the IndexedDB sync callback
  // (see the SDL_main.h note at the top), which is a plain browser callback
  // with no handler around it, so every single start of the game printed an
  // uncaught `unwind` to the console. A page that looks like it has thrown is
  // a page somebody reasonably believes has crashed.
  //
  // At nought the loop is registered, this returns, main returns, and the
  // runtime stays alive because SDL has already asked it to - the browser goes
  // on calling webFrame at the display's rate either way.
  emscripten_set_main_loop(webFrame, 0, 0);
#else
  while (running && !quitRequested)
  {
    runFrame();
  }

  shutdownGame();
#endif

  return 0;
}
