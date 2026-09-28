#include "pen.h"
#include "../lib/gfx.h"
#include "../lib/vector.h"

static const SDL_Color white = {255, 255, 255, 255};

void penReset(Pen *pen, float x, float y)
{
  SDL_zerop(pen);
  pen->x = x;
  pen->y = y;
}

void penFollow(Pen *pen, float x, float y, bool writing, float dt)
{
  float follow = 1.0f - expf(-dt * (writing ? 16.0f : 3.0f));

  pen->x += (x - pen->x) * follow;
  pen->y += (y - pen->y) * follow;
  pen->presence += ((writing ? 1.0f : 0.0f) - pen->presence) * (1.0f - expf(-dt * 6.0f));
}

void penSpark(Pen *pen, float x, float y, float speed)
{
  for (int i = 0; i < PEN_SPARKS; i++)
  {
    PenSpark *s = &pen->sparks[i];

    if (s->life <= 0)
    {
      float angle = frandRange(0, 2.0f * SDL_PI_F);
      float v = frandRange(0.3f, 1.0f) * speed;

      s->x = x;
      s->y = y;
      s->vx = cosf(angle) * v;
      s->vy = sinf(angle) * v - speed * 0.4f;
      s->maxLife = frandRange(0.25f, 0.6f);
      s->life = s->maxLife;
      return;
    }
  }
}

void updatePen(Pen *pen, float dt)
{
  pen->time += dt;

  for (int i = 0; i < PEN_SPARKS; i++)
  {
    PenSpark *s = &pen->sparks[i];

    if (s->life > 0)
    {
      s->life -= dt;
      s->x += s->vx * dt;
      s->y += s->vy * dt;
      s->vy += 60.0f * dt;
    }
  }
}

void drawStylus(float x, float y, float size, float spin, SDL_Color color, float alpha)
{
  static const Vec3 corners[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1.4f, 0},
                                  {0, -1.4f, 0}, {0, 0, 1}, {0, 0, -1}};
  static const int edges[12][2] = {{0, 2}, {0, 3}, {0, 4}, {0, 5}, {1, 2}, {1, 3},
                                   {1, 4}, {1, 5}, {2, 4}, {4, 3}, {3, 5}, {5, 2}};

  VecCamera cam = {x, y, size, 4.0f, 0};

  for (int i = 0; i < 12; i++)
  {
    Vec3 e0 = vec3Rotate(corners[edges[i][0]], spin * 3.0f, spin * 2.1f, 0);
    Vec3 e1 = vec3Rotate(corners[edges[i][1]], spin * 3.0f, spin * 2.1f, 0);

    vecLine3(&cam, e0, e1, 1.0f, color, alpha);
  }
}

void drawPen(const Pen *pen, const SDL_FPoint *from, SDL_Color tint, float alpha)
{
  SDL_Color bright = mixColor(tint, white, 0.5f);
  float p = pen->presence * alpha;

  if (p >= 0.01f)
  {
    if (from != NULL)
    {
      float t = pen->time;
      float flicker = 0.7f + 0.3f * sinf(t * 41.0f) * sinf(t * 23.0f);

      vecLine(from->x, from->y, pen->x, pen->y, 2.6f, tint, 0.14f * flicker * p);
      vecLine(from->x, from->y, pen->x, pen->y, 0.9f, bright, 0.75f * flicker * p);
    }

    drawStylus(pen->x, pen->y, 10.0f, pen->time, bright, p);
  }

  SDL_Color spark = mixColor(tint, white, 0.7f);

  for (int i = 0; i < PEN_SPARKS; i++)
  {
    const PenSpark *s = &pen->sparks[i];

    if (s->life > 0)
    {
      vecLine(s->x, s->y, s->x - s->vx * 0.04f, s->y - s->vy * 0.04f, 0.8f, spark,
              s->life / s->maxLife * alpha);
    }
  }

  vecFlush();

  if (p >= 0.01f)
  {
    SDL_SetTextureColorMod(texGlow, bright.r, bright.g, bright.b);
    SDL_SetTextureAlphaMod(texGlow, (Uint8)(255 * clamp(0.7f * p, 0.0f, 1.0f)));
    SDL_FRect dst = {pen->x - 15, pen->y - 15, 30, 30};
    SDL_RenderTexture(renderer, texGlow, NULL, &dst);
    SDL_SetTextureColorMod(texGlow, 255, 255, 255);
    SDL_SetTextureAlphaMod(texGlow, 255);
  }
}
