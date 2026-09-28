#pragma once

#include <SDL3/SDL.h>

// An animated GIF writer, small enough to own: a median-cut quantizer that
// builds one 256-colour palette for a whole clip, and the LZW encoder the
// format wants. It needs no renderer and no window, so tests/test_gif.c can
// decode what it writes and compare.
//
// Frames arrive as RGB565 - two bytes a pixel, which is what keeps eight
// seconds of play in memory at a size worth keeping (see clip.c) - and leave
// as 8-bit indices into the shared palette.

#define GIF_PALETTE_SIZE 256

// Builds a palette for every pixel of every frame, and a table from each RGB565
// value to the palette entry nearest it. Only the values that occur are looked
// up; the rest of `lookup` is left as it was.
void gifBuildPalette(const Uint16 *const *frames, int frameCount, int pixelsPerFrame,
                     Uint8 palette[GIF_PALETTE_SIZE * 3], Uint8 lookup[65536]);

typedef struct GifFrame
{
  const Uint8 *pixels; // width * height palette indices
  int delayCs;         // how long it stays up, in hundredths of a second
} GifFrame;

// Writes a looping GIF of `count` frames to `out`. Returns false if a write
// failed; what reached the stream by then is not a usable file.
bool gifWrite(SDL_IOStream *out, int width, int height,
              const Uint8 palette[GIF_PALETTE_SIZE * 3],
              const GifFrame *frames, int count);
