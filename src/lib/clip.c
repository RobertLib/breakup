#include "clip.h"
#include "gif.h"
#include "postfx.h"
#include "../ui/toast.h"
#include "../version.h"

#if BREAKUP_OFFERS_CLIPS

#define CLIP_W (SCREEN_WIDTH / 2)
#define CLIP_H (SCREEN_HEIGHT / 2)
#define CLIP_FPS 15
#define CLIP_SECONDS 8
#define CLIP_FRAMES (CLIP_FPS * CLIP_SECONDS)
#define CLIP_PIXELS (CLIP_W * CLIP_H)

enum
{
  JOB_IDLE,
  JOB_RUNNING,
  JOB_DONE,
  JOB_FAILED
};

// About 29 MB at RGB565, allocated the first time anything is recorded rather
// than at startup, so a session that never plays pays nothing for it.
static Uint16 *ring;
static Uint64 stamps[CLIP_FRAMES]; // SDL_GetTicks() at each capture
static int head;                   // the slot the next capture goes in
static int filled;

// The half-size copy of the frame that is actually read back. Reading the
// window itself meant a full-resolution copy off the GPU - 1600x1200 on a
// high-density display, nine milliseconds on the machine this was measured
// on, fifteen times a second - where this is a sixteenth of that.
static SDL_Texture *target;

static bool enabled = true;
static Uint64 lastCapture; // SDL_GetTicksNS() of the last frame taken

// Whether `target` holds a frame composited last time round that has not been
// read back yet. See clipBeginFrame().
static bool pending;
static Uint64 pendingTicks;

typedef struct ClipJob
{
  int first; // oldest frame's slot in the ring
  int count;
  int delays[CLIP_FRAMES];
  char path[512];
  bool inPictures;
} ClipJob;

static ClipJob job;
static SDL_Thread *thread;
static SDL_AtomicInt jobState;

void clipSetEnabled(bool value)
{
  enabled = value;
}

// Packs a read-back frame into RGB565. The target is made ARGB8888 and a
// renderer nearly always hands its own format back; anything else is
// converted first, which at this size costs little.
static bool storeFrame(SDL_Surface *shot, Uint16 *out)
{
  SDL_Surface *converted = NULL;

  if (shot->format != SDL_PIXELFORMAT_ARGB8888 && shot->format != SDL_PIXELFORMAT_XRGB8888)
  {
    converted = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_XRGB8888);

    if (converted == NULL)
    {
      return false;
    }

    shot = converted;
  }

  int w = SDL_min(shot->w, CLIP_W);
  int h = SDL_min(shot->h, CLIP_H);

  SDL_memset(out, 0, sizeof(Uint16) * CLIP_PIXELS);

  for (int y = 0; y < h; y++)
  {
    const Uint32 *row = (const Uint32 *)((const Uint8 *)shot->pixels + (size_t)y * shot->pitch);

    for (int x = 0; x < w; x++)
    {
      Uint32 p = row[x];

      out[y * CLIP_W + x] = (Uint16)((((p >> 16) & 0xF8) << 8) |
                                     (((p >> 8) & 0xFC) << 3) |
                                     ((p & 0xF8) >> 3));
    }
  }

  SDL_DestroySurface(converted);

  return true;
}

void clipCaptureFrame(void (*overlay)(void))
{
  // Nothing is recorded while a save is reading the ring.
  if (!enabled || SDL_GetAtomicInt(&jobState) != JOB_IDLE)
  {
    return;
  }

  Uint64 now = SDL_GetTicksNS();

  // A millisecond of slack, so that a display running at exactly sixty does
  // not miss every fourth frame by a rounding error and fall to twelve.
  if (filled > 0 && now - lastCapture + SDL_NS_PER_MS < SDL_NS_PER_SECOND / CLIP_FPS)
  {
    return;
  }

  if (ring == NULL)
  {
    ring = (Uint16 *)SDL_malloc(sizeof(Uint16) * CLIP_PIXELS * CLIP_FRAMES);

    if (ring == NULL)
    {
      return;
    }
  }

  if (target == NULL)
  {
    target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                               SDL_TEXTUREACCESS_TARGET, CLIP_W, CLIP_H);

    if (target == NULL)
    {
      return;
    }

    SDL_SetTextureScaleMode(target, SDL_SCALEMODE_LINEAR);
  }

  if (pending || !compositeSceneInto(target))
  {
    return;
  }

  // What the playing screen drew over the scene, drawn over it again here in
  // the same 800x600 it was drawn in the first time.
  if (overlay != NULL)
  {
    SDL_SetRenderLogicalPresentation(renderer, SCREEN_WIDTH, SCREEN_HEIGHT,
                                     SDL_LOGICAL_PRESENTATION_STRETCH);
    overlay();
  }

  SDL_SetRenderTarget(renderer, NULL);
  SDL_SetRenderLogicalPresentation(renderer, SCREEN_WIDTH, SCREEN_HEIGHT,
                                   SDL_LOGICAL_PRESENTATION_LETTERBOX);

  pending = true;
  pendingTicks = SDL_GetTicks();
  lastCapture = now;
}

void clipBeginFrame(void)
{
  if (!pending)
  {
    return;
  }

  pending = false;

  // A save that started since the frame was composited owns the ring now, and
  // the slot this would go in is the oldest frame it is encoding.
  if (SDL_GetAtomicInt(&jobState) != JOB_IDLE)
  {
    return;
  }

  // A read-back waits for the GPU to finish everything queued ahead of it.
  // Asked for in the frame that drew it, that is the whole of that frame -
  // three and a half milliseconds of standing still, measured, every fourth
  // frame. Asked for here, before this frame has queued anything, the frame it
  // is waiting on has been presented and is long done, and what is left is
  // about two.
  if (!SDL_SetRenderTarget(renderer, target))
  {
    return;
  }

  SDL_Surface *shot = SDL_RenderReadPixels(renderer, NULL);

  SDL_SetRenderTarget(renderer, NULL);

  if (shot == NULL)
  {
    return;
  }

  if (storeFrame(shot, &ring[(size_t)head * CLIP_PIXELS]))
  {
    stamps[head] = pendingTicks;
    head = (head + 1) % CLIP_FRAMES;
    filled = SDL_min(filled + 1, CLIP_FRAMES);
  }

  SDL_DestroySurface(shot);
}

static int SDLCALL encodeJob(UNUSED void *userdata)
{
  const Uint16 *frames565[CLIP_FRAMES];
  GifFrame frames[CLIP_FRAMES];
  Uint8 palette[GIF_PALETTE_SIZE * 3];
  Uint8 *lookup = (Uint8 *)SDL_calloc(65536, 1);
  Uint8 *indexed = (Uint8 *)SDL_malloc((size_t)CLIP_PIXELS * job.count);
  bool ok = lookup != NULL && indexed != NULL;

  if (ok)
  {
    for (int i = 0; i < job.count; i++)
    {
      frames565[i] = &ring[(size_t)((job.first + i) % CLIP_FRAMES) * CLIP_PIXELS];
    }

    gifBuildPalette(frames565, job.count, CLIP_PIXELS, palette, lookup);

    for (int i = 0; i < job.count; i++)
    {
      Uint8 *dst = &indexed[(size_t)i * CLIP_PIXELS];

      for (int p = 0; p < CLIP_PIXELS; p++)
      {
        dst[p] = lookup[frames565[i][p]];
      }

      frames[i] = (GifFrame){dst, job.delays[i]};
    }

    SDL_IOStream *out = SDL_IOFromFile(job.path, "wb");

    ok = out != NULL && gifWrite(out, CLIP_W, CLIP_H, palette, frames, job.count);

    if (out != NULL && !SDL_CloseIO(out))
    {
      ok = false;
    }

    if (!ok)
    {
      SDL_RemovePath(job.path);
    }
  }

  SDL_free(lookup);
  SDL_free(indexed);

  SDL_SetAtomicInt(&jobState, ok ? JOB_DONE : JOB_FAILED);

  return 0;
}

// A "Breakup" folder under Pictures, or the data folder the save lives in.
static bool chooseDirectory(char *out, size_t size, bool *inPictures)
{
  // Development helper: somewhere that is not the Pictures folder of whoever
  // is testing this. Taken as given - it must exist and end in a separator.
  const char *override = SDL_getenv("BREAKUP_CLIP_DIR");

  if (override != NULL)
  {
    SDL_strlcpy(out, override, size);
    *inPictures = false;
    return true;
  }

  const char *pictures = SDL_GetUserFolder(SDL_FOLDER_PICTURES);

  if (pictures != NULL)
  {
    int len = snprintf(out, size, "%s%s", pictures, BREAKUP_APP_NAME);

    if (len > 0 && (size_t)len < size - 2 && SDL_CreateDirectory(out))
    {
      SDL_strlcat(out, "/", size);
      *inPictures = true;
      return true;
    }
  }

  char *pref = SDL_GetPrefPath(BREAKUP_SAVE_ORG, BREAKUP_SAVE_APP);

  if (pref == NULL)
  {
    return false;
  }

  SDL_strlcpy(out, pref, size);
  SDL_free(pref);
  *inPictures = false;

  return true;
}

void clipSave(void)
{
  SDL_Color info = {140, 235, 255, 255};

  if (SDL_GetAtomicInt(&jobState) != JOB_IDLE)
  {
    pushToast("STILL SAVING THE LAST CLIP...", info);
    return;
  }

  if (ring == NULL || filled < CLIP_FPS)
  {
    pushToast("NOTHING TO SAVE YET - PLAY A MOMENT FIRST", info);
    return;
  }

  char dir[400];
  bool inPictures = false;

  if (!chooseDirectory(dir, sizeof(dir), &inPictures))
  {
    pushToast("COULD NOT FIND ANYWHERE TO SAVE THE CLIP", (SDL_Color){255, 130, 110, 255});
    return;
  }

  char stamp[32] = "clip";
  SDL_Time now;
  SDL_DateTime date;

  if (SDL_GetCurrentTime(&now) && SDL_TimeToDateTime(now, &date, true))
  {
    snprintf(stamp, sizeof(stamp), "%04d%02d%02d-%02d%02d%02d", date.year,
             date.month, date.day, date.hour, date.minute, date.second);
  }

  job.count = filled;
  job.first = (head - filled + CLIP_FRAMES) % CLIP_FRAMES;
  job.inPictures = inPictures;
  snprintf(job.path, sizeof(job.path), "%sbreakup-%s.gif", dir, stamp);

  // Each frame is held for the time until the next one was taken, rounded to
  // the hundredths of a second GIF counts in with the remainder carried, so
  // eight seconds of play is eight seconds of GIF whatever the frame rate was.
  double carry = 0;

  for (int i = 0; i < job.count; i++)
  {
    int slot = (job.first + i) % CLIP_FRAMES;
    int nextSlot = (slot + 1) % CLIP_FRAMES;
    double ms = i + 1 < job.count ? (double)(stamps[nextSlot] - stamps[slot])
                                  : 1000.0 / CLIP_FPS;
    double cs = SDL_min(ms, 250.0) / 10.0 + carry;
    int whole = (int)cs;

    job.delays[i] = SDL_max(whole, 2);
    carry = cs - job.delays[i];
  }

  SDL_SetAtomicInt(&jobState, JOB_RUNNING);
  thread = SDL_CreateThread(encodeJob, "breakup-clip", NULL);

  if (thread == NULL)
  {
    SDL_SetAtomicInt(&jobState, JOB_IDLE);
    pushToast("COULD NOT SAVE THE CLIP", (SDL_Color){255, 130, 110, 255});
    return;
  }

  pushToast("SAVING THE LAST 8 SECONDS...", info);
}

void clipUpdate(void)
{
  int state = SDL_GetAtomicInt(&jobState);

  if (state != JOB_DONE && state != JOB_FAILED)
  {
    return;
  }

  SDL_WaitThread(thread, NULL);
  thread = NULL;

  if (state == JOB_DONE)
  {
    printf("Wrote %s\n", job.path);
    pushToast(job.inPictures ? "CLIP SAVED TO PICTURES/BREAKUP"
                             : "CLIP SAVED TO THE GAME'S DATA FOLDER",
              (SDL_Color){120, 255, 160, 255});
  }
  else
  {
    fprintf(stderr, "Could not write %s\n", job.path);
    pushToast("COULD NOT SAVE THE CLIP", (SDL_Color){255, 130, 110, 255});
  }

  // The next clip starts from here rather than reaching back across the save.
  filled = 0;
  SDL_SetAtomicInt(&jobState, JOB_IDLE);
}

void clipShutdown(void)
{
  if (thread != NULL)
  {
    SDL_WaitThread(thread, NULL);
    thread = NULL;
  }

  if (target != NULL)
  {
    SDL_DestroyTexture(target);
    target = NULL;
  }

  SDL_free(ring);
  ring = NULL;
  filled = 0;
  SDL_SetAtomicInt(&jobState, JOB_IDLE);
}

#else

void clipSetEnabled(UNUSED bool value)
{
}

void clipCaptureFrame(UNUSED void (*overlay)(void))
{
}

void clipBeginFrame(void)
{
}

void clipSave(void)
{
}

void clipUpdate(void)
{
}

void clipShutdown(void)
{
}

#endif
