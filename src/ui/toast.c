#include "toast.h"

#define MAX_TOASTS 4
#define TOAST_LIFE 3.2f
#define TOAST_FADE 0.35f
#define TOAST_TOP (STATUS_BAR_HEIGHT + 34.0f)
#define TOAST_STEP 30.0f

typedef struct Toast
{
  SDL_Texture *texture;
  SDL_Color color;
  float life;
} Toast;

// Oldest first. A fifth notice pushes the oldest one off rather than waiting
// behind it: the newest is the one the player has just caused.
static Toast toasts[MAX_TOASTS];
static int toastCount;

static void dropOldest(void)
{
  if (toastCount == 0)
  {
    return;
  }

  SDL_DestroyTexture(toasts[0].texture);

  for (int i = 1; i < toastCount; i++)
  {
    toasts[i - 1] = toasts[i];
  }

  toastCount--;
}

void pushToast(const char *text, SDL_Color color)
{
  if (renderer == NULL || font16 == NULL)
  {
    return;
  }

  if (toastCount == MAX_TOASTS)
  {
    dropOldest();
  }

  toasts[toastCount++] = (Toast){renderTextBlended(font16, text, color), color, TOAST_LIFE};
}

void updateToasts(void)
{
  for (int i = 0; i < toastCount; i++)
  {
    toasts[i].life -= (float)realDt;
  }

  while (toastCount > 0 && toasts[0].life <= 0)
  {
    dropOldest();
  }
}

void drawToasts(bool low)
{
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

  for (int i = 0; i < toastCount; i++)
  {
    const Toast *toast = &toasts[i];
    float t = toast->life;

    // Fades in over the first fraction of a second and out over the last.
    float alpha = fminf(1.0f, fminf((TOAST_LIFE - t) / TOAST_FADE, t / TOAST_FADE));
    alpha = clamp(alpha, 0.0f, 1.0f);

    SDL_FPoint size = getSize(toast->texture);
    float maxW = SCREEN_WIDTH - 60;
    float scale = size.x > maxW ? maxW / size.x : 1.0f;
    float w = size.x * scale;
    float h = size.y * scale;
    float y = low ? SCREEN_HEIGHT - 84 - (toastCount - 1 - i) * TOAST_STEP
                  : TOAST_TOP + i * TOAST_STEP;

    SDL_SetRenderDrawColor(renderer, 6, 8, 22, (Uint8)(215 * alpha));
    SDL_FRect pill = {SCREEN_WIDTH / 2.0f - w / 2 - 14, y - 6, w + 28, h + 12};
    SDL_RenderFillRect(renderer, &pill);

    SDL_SetRenderDrawColor(renderer, toast->color.r, toast->color.g,
                           toast->color.b, (Uint8)(200 * alpha));
    SDL_FRect edge = {pill.x, pill.y + pill.h - 2, pill.w, 2};
    SDL_RenderFillRect(renderer, &edge);

    SDL_SetTextureAlphaMod(toast->texture, (Uint8)(255 * alpha));
    SDL_FRect dst = {SCREEN_WIDTH / 2.0f - w / 2, y, w, h};
    SDL_RenderTexture(renderer, toast->texture, NULL, &dst);
    SDL_SetTextureAlphaMod(toast->texture, 255);
  }
}

void destroyToasts(void)
{
  while (toastCount > 0)
  {
    dropOldest();
  }
}
