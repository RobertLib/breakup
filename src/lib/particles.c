#include "particles.h"
#include "camera.h"
#include "gfx.h"

#define MAX_PARTICLES 768

typedef enum ParticleKind
{
  P_GLOW,  // additive radial glow, shrinks as it dies
  P_SHARD, // solid rotating square with gravity
  P_PUFF   // additive glow that grows as it fades
} ParticleKind;

typedef struct Particle
{
  Vec2 pos;
  Vec2 vel;
  float size;
  float life;
  float maxLife;
  float rot;
  float rotVel;
  SDL_Color color;
  ParticleKind kind;
  bool active;
} Particle;

static Particle particles[MAX_PARTICLES];
static int nextParticle;

static Particle *acquire(void)
{
  Particle *p = &particles[nextParticle];
  nextParticle = (nextParticle + 1) % MAX_PARTICLES;
  p->active = true;
  p->rot = 0;
  p->rotVel = 0;
  return p;
}

void clearParticles(void)
{
  for (int i = 0; i < MAX_PARTICLES; i++)
  {
    particles[i].active = false;
  }
}

void updateParticles(void)
{
  for (int i = 0; i < MAX_PARTICLES; i++)
  {
    Particle *p = &particles[i];

    if (!p->active)
    {
      continue;
    }

    p->life -= (float)dt;

    if (p->life <= 0)
    {
      p->active = false;
      continue;
    }

    if (p->kind == P_SHARD)
    {
      p->vel.y += 700.0f * (float)dt; // gravity
      p->rot += p->rotVel * (float)dt;
    }

    p->pos.x += p->vel.x * (float)dt;
    p->pos.y += p->vel.y * (float)dt;

    // Air drag for sparks
    if (p->kind == P_GLOW)
    {
      p->vel.x /= 1 + 3.0f * (float)dt;
      p->vel.y /= 1 + 3.0f * (float)dt;
    }
  }
}

void drawParticles(void)
{
  for (int i = 0; i < MAX_PARTICLES; i++)
  {
    Particle *p = &particles[i];

    if (!p->active)
    {
      continue;
    }

    float t = p->life / p->maxLife; // 1 -> 0
    float screenY = p->pos.y - camera.y;

    if (screenY < -80 || screenY > SCREEN_HEIGHT + 80)
    {
      continue;
    }

    switch (p->kind)
    {
    case P_GLOW:
    {
      float size = p->size * (0.4f + 0.6f * t);
      SDL_SetTextureColorMod(texGlow, p->color.r, p->color.g, p->color.b);
      SDL_SetTextureAlphaMod(texGlow, (Uint8)(220 * t));
      SDL_FRect dst = {p->pos.x - size / 2, screenY - size / 2, size, size};
      SDL_RenderTexture(renderer, texGlow, NULL, &dst);
      break;
    }
    case P_PUFF:
    {
      float size = p->size * (1.6f - 0.6f * t);
      SDL_SetTextureColorMod(texGlow, p->color.r, p->color.g, p->color.b);
      SDL_SetTextureAlphaMod(texGlow, (Uint8)(200 * t * t));
      SDL_FRect dst = {p->pos.x - size / 2, screenY - size / 2, size, size};
      SDL_RenderTexture(renderer, texGlow, NULL, &dst);
      break;
    }
    case P_SHARD:
    {
      float size = p->size * (0.5f + 0.5f * t);
      SDL_SetTextureColorMod(texSquare, p->color.r, p->color.g, p->color.b);
      SDL_SetTextureAlphaMod(texSquare, (Uint8)(255 * t));
      SDL_FRect dst = {p->pos.x - size / 2, screenY - size / 2, size, size};
      SDL_RenderTextureRotated(renderer, texSquare, NULL, &dst, p->rot, NULL, SDL_FLIP_NONE);
      break;
    }
    }
  }

  SDL_SetTextureColorMod(texGlow, 255, 255, 255);
  SDL_SetTextureAlphaMod(texGlow, 255);
  SDL_SetTextureColorMod(texSquare, 255, 255, 255);
  SDL_SetTextureAlphaMod(texSquare, 255);
}

void spawnBurst(float x, float y, SDL_Color color, int count, float speed)
{
  spawnImpactBurst(x, y, color, count, speed, (Vec2){0, 0});
}

void spawnImpactBurst(float x, float y, SDL_Color color, int count, float speed,
                      Vec2 dir)
{
  // A cone of about a hundred degrees around the direction of the blow, and a
  // fifth of the particles thrown anywhere at all so the spray does not read as
  // a solid wedge. Without a direction the cone opens to the full circle and
  // this is the burst it always was.
  bool aimed = dir.x != 0 || dir.y != 0;
  float centre = aimed ? atan2f(dir.y, dir.x) : 0;
  float spread = aimed ? 0.9f : SDL_PI_F;

  for (int i = 0; i < count; i++)
  {
    bool stray = aimed && i % 5 == 0;
    float ang = stray
                    ? frand() * 2.0f * SDL_PI_F
                    : centre + frandRange(-spread, spread);
    float mag = speed * frandRange(0.3f, 1.0f);

    Particle *p = acquire();
    p->pos = (Vec2){x, y};
    p->vel = (Vec2){cosf(ang) * mag, sinf(ang) * mag};
    p->color = color;

    if (i % 3 == 0)
    {
      p->kind = P_SHARD;
      p->size = frandRange(4, 9);
      p->maxLife = p->life = frandRange(0.5f, 0.9f);
      p->rotVel = frandRange(-540, 540);
      p->vel.y -= speed * 0.4f; // shards pop upward
    }
    else
    {
      p->kind = P_GLOW;
      p->size = frandRange(10, 26);
      p->maxLife = p->life = frandRange(0.25f, 0.55f);
    }
  }
}

void spawnGlowPuff(float x, float y, SDL_Color color, float size, float life)
{
  Particle *p = acquire();
  p->pos = (Vec2){x, y};
  p->vel = (Vec2){0, 0};
  p->color = color;
  p->kind = P_PUFF;
  p->size = size;
  p->maxLife = p->life = life;
}

void spawnTrail(float x, float y, SDL_Color color, float size)
{
  Particle *p = acquire();
  p->pos = (Vec2){x, y};
  p->vel = (Vec2){frandRange(-8, 8), frandRange(-8, 8)};
  p->color = color;
  p->kind = P_GLOW;
  p->size = size;
  p->maxLife = p->life = 0.3f;
}

void spawnFirework(float x, float y)
{
  SDL_Color color = hsvColor(frand() * 360.0f, 0.8f, 1.0f);
  spawnBurst(x, y, color, 42, 260);
  spawnGlowPuff(x, y, color, 120, 0.5f);
}
