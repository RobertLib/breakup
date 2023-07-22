#include "postfx.h"

// Bloom resolutions, as divisors of the scene texture. The bright pass runs at
// a half-size copy; the two blur levels are what gives the glow its falloff -
// a tight halo from the first and a wide haze from the second, which is
// cheaper and smoother than one big blur kernel.
#define BRIGHT_DIV 2
#define BLUR_DIV 8
#define WIDE_DIV 20

static SDL_Texture *texScene;
static SDL_Texture *texBright;
static SDL_Texture *texBlur;
static SDL_Texture *texWide;

// Size of the scene texture, in output pixels, and where it lands in the
// window. Both follow the window: a resize or a fullscreen toggle rebuilds
// every target at the new size rather than stretching the old one.
static int sceneW, sceneH;
static SDL_FRect presentRect;

static bool active;

static SDL_Texture *makeTarget(int w, int h)
{
  SDL_Texture *tex = SDL_CreateTexture(
      renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
      w < 1 ? 1 : w, h < 1 ? 1 : h);

  if (tex == NULL)
  {
    return NULL;
  }

  // Linear, because every one of these is sampled at a size it was not drawn
  // at: the blur *is* the filtering, and the scene itself lands on sub-pixel
  // offsets as soon as the camera shakes.
  SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);

  return tex;
}

static void releaseTargets(void)
{
  SDL_Texture **all[] = {&texScene, &texBright, &texBlur, &texWide};

  for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++)
  {
    if (*all[i] != NULL)
    {
      SDL_DestroyTexture(*all[i]);
      *all[i] = NULL;
    }
  }

  sceneW = 0;
  sceneH = 0;
}

// Builds the four targets for the current window, or tears the lot down and
// reports failure so the caller can draw straight to the window instead.
static bool ensureTargets(void)
{
  SDL_FRect rect;

  if (!SDL_GetRenderLogicalPresentationRect(renderer, &rect))
  {
    return false;
  }

  int w = (int)SDL_lroundf(rect.w);
  int h = (int)SDL_lroundf(rect.h);

  if (w < 1 || h < 1)
  {
    return false;
  }

  presentRect = rect;

  if (texScene != NULL && w == sceneW && h == sceneH)
  {
    return true;
  }

  releaseTargets();

  texScene = makeTarget(w, h);
  texBright = makeTarget(w / BRIGHT_DIV, h / BRIGHT_DIV);
  texBlur = makeTarget(w / BLUR_DIV, h / BLUR_DIV);
  texWide = makeTarget(w / WIDE_DIV, h / WIDE_DIV);

  if (texScene == NULL || texBright == NULL || texBlur == NULL || texWide == NULL)
  {
    releaseTargets();
    return false;
  }

  sceneW = w;
  sceneH = h;

  return true;
}

void initializePostFx(void)
{
  releaseTargets();
  active = false;
}

void destroyPostFx(void)
{
  releaseTargets();
  active = false;
}

bool beginScene(void)
{
  // A scene left open means some draw path returned without compositing it,
  // and the cost of that is not a missing effect but a black window: the
  // renderer would still be pointed at a texture nobody ever blits. Putting it
  // back is a cheap way to make that a glitch rather than a dead screen.
  if (active)
  {
    SDL_SetRenderTarget(renderer, NULL);
    SDL_SetRenderLogicalPresentation(renderer, SCREEN_WIDTH, SCREEN_HEIGHT,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    active = false;
  }

  if (!ensureTargets())
  {
    return false;
  }

  if (!SDL_SetRenderTarget(renderer, texScene))
  {
    return false;
  }

  // Each render target carries its own logical presentation, so the world can
  // go on being drawn in 800x600 while the texture underneath it is however
  // many pixels the display actually has. STRETCH rather than LETTERBOX: the
  // texture was cut to the letterbox rect already, so its aspect is exact and
  // a second letterbox would only add bars inside the bars.
  SDL_SetRenderLogicalPresentation(renderer, SCREEN_WIDTH, SCREEN_HEIGHT,
                                   SDL_LOGICAL_PRESENTATION_STRETCH);

  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);

  active = true;

  return true;
}

// Draws src over the whole of the current target.
static void blit(SDL_Texture *src, SDL_BlendMode blend, Uint8 alpha)
{
  SDL_SetTextureBlendMode(src, blend);
  SDL_SetTextureAlphaMod(src, alpha);
  SDL_RenderTexture(renderer, src, NULL, NULL);
  SDL_SetTextureAlphaMod(src, 255);
}

// The bright pass. Multiplying the scene by itself is the threshold: a pixel at
// a tenth of full brightness comes back at a hundredth and vanishes, while a
// pixel near white barely moves. That is what keeps the glow on the neon and
// off the nebula behind it, and it costs a blit rather than a shader.
//
// Twice, for the cube rather than the square, because the square was not
// selective enough: the nebula sits around a third of full brightness, which
// squares to a ninth - dim, but spread over the whole screen, and it lifted the
// entire picture into a haze. Cubed it comes back at a twenty-seventh and stays
// where it belongs, while a white brick edge loses almost nothing.
static void buildBloom(void)
{
  SDL_SetRenderTarget(renderer, texBright);
  SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
  blit(texScene, SDL_BLENDMODE_NONE, 255);
  blit(texScene, SDL_BLENDMODE_MUL, 255);
  blit(texScene, SDL_BLENDMODE_MUL, 255);

  // Downsampling with a linear filter is the blur, and doing it twice at two
  // scales gives the halo an inner core and an outer haze.
  SDL_SetRenderTarget(renderer, texBlur);
  blit(texBright, SDL_BLENDMODE_NONE, 255);

  SDL_SetRenderTarget(renderer, texWide);
  blit(texBlur, SDL_BLENDMODE_NONE, 255);
}

void endScene(const PostFx *fx)
{
  if (!active)
  {
    return;
  }

  active = false;

  buildBloom();

  SDL_SetRenderTarget(renderer, NULL);

  // Composite in output pixels rather than logical ones, so the scene texture
  // lands on the display one texel to one pixel. Going through the window's own
  // 800x600 presentation would scale it down and back up again, and hand back a
  // softer picture than the one the game drew.
  SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);

  float scale = presentRect.w / (float)SCREEN_WIDTH;

  // Shake and roll would otherwise swing the edges of the scene inside the
  // frame and show the black behind it, so the same impact that moves the
  // camera also pushes it in far enough to cover the move.
  float cover = 1.0f +
                (fabsf(fx->offsetX) + fabsf(fx->offsetY)) / 260.0f +
                fabsf(fx->angle) / 26.0f;
  float zoom = fx->zoom * cover;

  SDL_FRect dst = {
      presentRect.x + presentRect.w / 2 - presentRect.w * zoom / 2 + fx->offsetX * scale,
      presentRect.y + presentRect.h / 2 - presentRect.h * zoom / 2 + fx->offsetY * scale,
      presentRect.w * zoom,
      presentRect.h * zoom};

  // Clipped to where the game's frame belongs, so a shake cannot spill into the
  // letterbox bars around it.
  SDL_Rect clip = {
      (int)SDL_lroundf(presentRect.x), (int)SDL_lroundf(presentRect.y),
      (int)SDL_lroundf(presentRect.w), (int)SDL_lroundf(presentRect.h)};
  SDL_SetRenderClipRect(renderer, &clip);

  SDL_SetTextureBlendMode(texScene, SDL_BLENDMODE_NONE);
  SDL_RenderTextureRotated(renderer, texScene, NULL, &dst, fx->angle, NULL,
                           SDL_FLIP_NONE);

  // Chromatic split: the red and blue channels of the same frame, additively
  // over it and a couple of pixels apart. Only the heaviest impacts ask for
  // any, and at zero it costs nothing at all.
  if (fx->split > 0.01f)
  {
    float shift = fx->split * 3.0f * scale;
    Uint8 alpha = (Uint8)(90 * fx->split);

    const SDL_Color channels[2] = {{255, 40, 40, 0}, {40, 90, 255, 0}};

    for (int i = 0; i < 2; i++)
    {
      SDL_FRect ghost = dst;
      ghost.x += i == 0 ? -shift : shift;

      SDL_SetTextureBlendMode(texScene, SDL_BLENDMODE_ADD);
      SDL_SetTextureColorMod(texScene, channels[i].r, channels[i].g, channels[i].b);
      SDL_SetTextureAlphaMod(texScene, alpha);
      SDL_RenderTextureRotated(renderer, texScene, NULL, &ghost, fx->angle, NULL,
                               SDL_FLIP_NONE);
    }

    SDL_SetTextureColorMod(texScene, 255, 255, 255);
    SDL_SetTextureAlphaMod(texScene, 255);
  }

  if (fx->bloom > 0.01f)
  {
    SDL_SetTextureBlendMode(texBlur, SDL_BLENDMODE_ADD);
    SDL_SetTextureAlphaMod(texBlur, (Uint8)(165 * fx->bloom));
    SDL_RenderTextureRotated(renderer, texBlur, NULL, &dst, fx->angle, NULL,
                             SDL_FLIP_NONE);

    SDL_SetTextureBlendMode(texWide, SDL_BLENDMODE_ADD);
    SDL_SetTextureAlphaMod(texWide, (Uint8)(115 * fx->bloom));
    SDL_RenderTextureRotated(renderer, texWide, NULL, &dst, fx->angle, NULL,
                             SDL_FLIP_NONE);

    SDL_SetTextureAlphaMod(texBlur, 255);
    SDL_SetTextureAlphaMod(texWide, 255);
  }

  SDL_SetRenderClipRect(renderer, NULL);

  // Back to the state every other screen draws in.
  SDL_SetRenderLogicalPresentation(renderer, SCREEN_WIDTH, SCREEN_HEIGHT,
                                   SDL_LOGICAL_PRESENTATION_LETTERBOX);
}
