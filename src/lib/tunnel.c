#include "tunnel.h"

#define RINGS 12
#define SIDES 6

// How far down the tunnel a star starts, and where it is let go of behind the
// eye.
#define STAR_FAR -26.0f
#define STAR_NEAR 1.8f

static void resetStar(TunnelStar *s, bool anywhere)
{
  s->p = vec3(frandRange(-6.0f, 6.0f), frandRange(-4.5f, 4.5f),
              anywhere ? frandRange(STAR_FAR, 1.5f) : STAR_FAR);
  s->speed = frandRange(4.0f, 9.0f);
}

void tunnelReset(Tunnel *t)
{
  t->time = 0;

  for (int i = 0; i < TUNNEL_STARS; i++)
  {
    resetStar(&t->stars[i], true);
  }
}

void updateTunnel(Tunnel *t, float dt)
{
  t->time += dt;

  for (int i = 0; i < TUNNEL_STARS; i++)
  {
    t->stars[i].p.z += t->stars[i].speed * dt;

    if (t->stars[i].p.z > STAR_NEAR)
    {
      resetStar(&t->stars[i], false);
    }
  }
}

void drawTunnel(const Tunnel *t, SDL_Color tint, float alpha, float cx, float cy)
{
  if (alpha <= 0.004f)
  {
    return;
  }

  VecCamera cam = {cx, cy, 70.0f, 3.0f, 0};

  // Each ring comes one place nearer every two and a bit seconds; when the
  // nearest has gone past the eye they have all moved up one, and the one at
  // the far end has faded in out of nothing, so the loop has no seam.
  float drift = fmodf(t->time * 0.45f, 1.0f);
  SDL_FPoint prev[SIDES];
  float prevA = 0;
  bool havePrev = false;

  for (int k = 0; k < RINGS; k++)
  {
    float depth = k + 1 - drift;
    float z = 1.0f - depth * 2.0f;
    float nearFade = clamp((1.2f - z) / 2.5f, 0.0f, 1.0f);
    float farFade = 1.0f - depth / RINGS;
    float ringA = 0.22f * nearFade * farFade * alpha;
    float rot = z * 0.12f + t->time * 0.06f;
    SDL_FPoint pts[SIDES];
    bool ok = true;

    for (int s = 0; s < SIDES; s++)
    {
      float angle = rot + s * 2.0f * SDL_PI_F / SIDES;

      ok = ok && vecProject(&cam, vec3(cosf(angle) * 4.5f, sinf(angle) * 3.2f, z), &pts[s], NULL);
    }

    if (!ok)
    {
      havePrev = false;
      continue;
    }

    for (int s = 0; s < SIDES; s++)
    {
      int n = (s + 1) % SIDES;

      vecLine(pts[s].x, pts[s].y, pts[n].x, pts[n].y, 1.0f, tint, ringA);

      if (havePrev)
      {
        vecLine(pts[s].x, pts[s].y, prev[s].x, prev[s].y, 0.8f, tint,
                fminf(ringA, prevA) * 0.6f);
      }
    }

    SDL_memcpy(prev, pts, sizeof(prev));
    prevA = ringA;
    havePrev = true;
  }

  // The stars go down the middle of the screen whatever the rings are doing,
  // or they would swing about with the face the rings follow.
  VecCamera starCam = {SCREEN_WIDTH / 2.0f, cy, 70.0f, 3.0f, 0};
  SDL_Color color = mixColor(tint, (SDL_Color){255, 255, 255, 255}, 0.6f);

  for (int i = 0; i < TUNNEL_STARS; i++)
  {
    const TunnelStar *s = &t->stars[i];
    SDL_FPoint head, tail;
    Vec3 back = vec3(s->p.x, s->p.y, s->p.z - 0.3f - s->speed * 0.05f);

    if (!vecProject(&starCam, s->p, &head, NULL) || !vecProject(&starCam, back, &tail, NULL))
    {
      continue;
    }

    float fade = clamp((s->p.z - STAR_FAR) / 8.0f, 0.0f, 1.0f);

    vecLine(tail.x, tail.y, head.x, head.y, 0.9f, color, 0.5f * fade * alpha);
  }

  vecFlush();
}
