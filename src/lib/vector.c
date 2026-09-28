#include "vector.h"

// Two strips of three rows each - edge, centre, edge - is six vertices a strip
// and four triangles, so a line is twelve of one and twenty-four of the other.
#define VERTS_PER_LINE 12
#define INDICES_PER_LINE 24

// A face is a few hundred lines and the tunnel behind it another couple of
// hundred; a batch bigger than a frame's worth just flushes itself early.
#define MAX_LINES 1024

static SDL_Vertex vertices[MAX_LINES * VERTS_PER_LINE];
static int indices[MAX_LINES * INDICES_PER_LINE];
static int lineCount;

Vec3 vec3Rotate(Vec3 p, float yaw, float pitch, float roll)
{
  float c = cosf(yaw), s = sinf(yaw);
  Vec3 a = {p.x * c + p.z * s, p.y, -p.x * s + p.z * c};

  c = cosf(pitch);
  s = sinf(pitch);
  Vec3 b = {a.x, a.y * c - a.z * s, a.y * s + a.z * c};

  c = cosf(roll);
  s = sinf(roll);

  return (Vec3){b.x * c - b.y * s, b.x * s + b.y * c, b.z};
}

bool vecProject(const VecCamera *cam, Vec3 p, SDL_FPoint *out, float *depth)
{
  float z = cam->distance - p.z;

  if (z <= 0.05f)
  {
    return false;
  }

  float f = cam->distance / z;

  out->x = cam->cx + p.x * f * cam->scale;
  out->y = cam->cy - p.y * f * cam->scale;

  if (depth != NULL)
  {
    *depth = f;
  }

  return true;
}

static SDL_FColor fcolor(SDL_Color c, float alpha)
{
  return (SDL_FColor){c.r / 255.0f, c.g / 255.0f, c.b / 255.0f,
                      clamp(alpha, 0.0f, 1.0f)};
}

// One strip: the colour down the middle of it, nothing at either edge.
static void strip(SDL_Vertex *v, int *idx, int base, float x0, float y0,
                  float x1, float y1, float nx, float ny, float half,
                  SDL_FColor c)
{
  SDL_FColor clear = {c.r, c.g, c.b, 0};

  v[0] = (SDL_Vertex){{x0 + nx * half, y0 + ny * half}, clear, {0, 0}};
  v[1] = (SDL_Vertex){{x0, y0}, c, {0, 0}};
  v[2] = (SDL_Vertex){{x0 - nx * half, y0 - ny * half}, clear, {0, 0}};
  v[3] = (SDL_Vertex){{x1 + nx * half, y1 + ny * half}, clear, {0, 0}};
  v[4] = (SDL_Vertex){{x1, y1}, c, {0, 0}};
  v[5] = (SDL_Vertex){{x1 - nx * half, y1 - ny * half}, clear, {0, 0}};

  const int quads[2][4] = {{0, 1, 4, 3}, {1, 2, 5, 4}};

  for (int q = 0; q < 2; q++)
  {
    int *out = &idx[q * 6];

    out[0] = base + quads[q][0];
    out[1] = base + quads[q][1];
    out[2] = base + quads[q][2];
    out[3] = base + quads[q][0];
    out[4] = base + quads[q][2];
    out[5] = base + quads[q][3];
  }
}

void vecLine(float x0, float y0, float x1, float y1, float width,
             SDL_Color color, float alpha)
{
  if (alpha <= 0.004f)
  {
    return;
  }

  float dx = x1 - x0;
  float dy = y1 - y0;
  float length = sqrtf(dx * dx + dy * dy);

  // A line with no length has no direction to be widened across. It is still
  // a point of light, so it is given a stub of one - which is what a vertex
  // where three lines meet on edge looks like from the side.
  if (length < 0.01f)
  {
    dx = 0.01f;
    dy = 0;
    length = 0.01f;
  }

  if (lineCount >= MAX_LINES)
  {
    vecFlush();
  }

  float nx = -dy / length;
  float ny = dx / length;

  // Stretched a touch past both ends, so that the joints of a polyline close
  // up instead of leaving a notch at every corner - and only a touch, because
  // what overlaps is drawn twice, additively, and a curve made of short lines
  // turns into a string of beads.
  float ex = dx / length * width * 0.15f;
  float ey = dy / length * width * 0.15f;

  int base = lineCount * VERTS_PER_LINE;
  SDL_Vertex *v = &vertices[base];
  int *idx = &indices[lineCount * INDICES_PER_LINE];

  strip(v, idx, base, x0 - ex, y0 - ey, x1 + ex, y1 + ey, nx, ny,
        width * 1.5f, fcolor(color, alpha * 0.32f));
  strip(v + 6, idx + 12, base + 6, x0 - ex, y0 - ey, x1 + ex, y1 + ey, nx, ny,
        width * 0.5f + 0.35f, fcolor(color, alpha));

  lineCount++;
}

void vecLine3(const VecCamera *cam, Vec3 a, Vec3 b, float width,
              SDL_Color color, float alpha)
{
  SDL_FPoint pa, pb;
  float da, db;

  if (!vecProject(cam, a, &pa, &da) || !vecProject(cam, b, &pb, &db))
  {
    return;
  }

  float depth = (da + db) * 0.5f;

  if (cam->fog > 0)
  {
    alpha *= clamp(1.0f - cam->fog * (1.0f - depth), 0.08f, 1.0f);
  }

  vecLine(pa.x, pa.y, pb.x, pb.y, width * clamp(depth, 0.35f, 1.6f), color, alpha);
}

void vecPolygon(float cx, float cy, float radius, int sides, float angle,
                float width, SDL_Color color, float alpha)
{
  float step = 2.0f * SDL_PI_F / (float)sides;

  for (int i = 0; i < sides; i++)
  {
    float a0 = angle + step * i;
    float a1 = angle + step * (i + 1);

    vecLine(cx + cosf(a0) * radius, cy + sinf(a0) * radius,
            cx + cosf(a1) * radius, cy + sinf(a1) * radius, width, color, alpha);
  }
}

void vecFlush(void)
{
  if (lineCount == 0)
  {
    return;
  }

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
  SDL_RenderGeometry(renderer, NULL, vertices, lineCount * VERTS_PER_LINE,
                     indices, lineCount * INDICES_PER_LINE);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

  lineCount = 0;
}

SDL_Color mixColor(SDL_Color a, SDL_Color b, float t)
{
  t = clamp(t, 0.0f, 1.0f);

  return (SDL_Color){(Uint8)(a.r + (b.r - a.r) * t),
                     (Uint8)(a.g + (b.g - a.g) * t),
                     (Uint8)(a.b + (b.b - a.b) * t),
                     255};
}
