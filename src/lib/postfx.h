#pragma once

#include "../globals.h"

// Full-frame post-processing for the playing field.
//
// The world is drawn into an offscreen scene texture instead of straight to the
// window, which buys three things the 2D renderer cannot do in one pass:
//
//   - bloom, so the neon actually bleeds light instead of stopping at its own
//     edge. There is no shader here: a half-size copy of the scene multiplied
//     by itself is a bright pass (dark pixels fall off as the square, bright
//     ones survive), and downsampling that with linear filtering is the blur.
//   - a camera that can shake in *both* axes and roll, rather than the vertical
//     jitter that came of nudging camera.y before drawing the world.
//   - a hit zoom, and a chromatic split on the heaviest impacts.
//
// The scene texture is created at the renderer's own output size, with its own
// logical presentation over it, so none of this costs the game the high-DPI
// sharpness it draws its sprites at.

// Camera state applied when the scene is composited.
typedef struct PostFx
{
  float offsetX; // shake, in logical pixels
  float offsetY;
  float angle;   // shake roll, in degrees
  float zoom;    // 1.0 = untouched
  float split;   // chromatic aberration, 0..1
  float bloom;   // bloom strength, 0..1
} PostFx;

void initializePostFx(void);

void destroyPostFx(void);

// Redirects rendering into the scene texture. Returns false if the renderer
// could not give us one, in which case the caller draws to the window as
// before and endScene() is not called.
bool beginScene(void);

// Composites the scene back onto the window and restores the render state.
void endScene(const PostFx *fx);
