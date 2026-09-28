#pragma once

#include "../globals.h"

// Glowing vector lines, and the little bit of 3D it takes to put them in
// perspective.
//
// Everything the story scenes draw - the Sovereign's face, the tunnel behind
// it, the map of the run under its writing - is lines. SDL_RenderLine is one
// pixel wide and hard-edged, which under the bloom reads as a scratch rather
// than as a beam of light, so a line here is two strips of triangles instead:
// a narrow bright core and a wide soft halo, each with its colour down the
// middle and nothing at its edges. They are batched and handed to
// SDL_RenderGeometry in as few calls as the batch allows, additively, so where
// two lines cross they get brighter the way light does.

typedef struct Vec3
{
  float x, y, z; // y up, z towards the viewer
} Vec3;

// Where the origin lands on the screen, how many pixels one unit is at the
// origin's depth, and how far the eye is from it. `fog` dims what is further
// away than the origin: 0 leaves it alone, 2 or so makes the back of a head
// read as the back of it.
typedef struct VecCamera
{
  float cx, cy;
  float scale;
  float distance;
  float fog;
} VecCamera;

static inline Vec3 vec3(float x, float y, float z)
{
  return (Vec3){x, y, z};
}

static inline Vec3 vec3Add(Vec3 a, Vec3 b)
{
  return (Vec3){a.x + b.x, a.y + b.y, a.z + b.z};
}

static inline Vec3 vec3Scale(Vec3 a, float s)
{
  return (Vec3){a.x * s, a.y * s, a.z * s};
}

// Yaw about y, then pitch about x, then roll about z, in radians. A positive
// yaw turns the front of a thing to the right of the screen and a positive
// pitch tips it down, which is what "look at" wants from both.
Vec3 vec3Rotate(Vec3 p, float yaw, float pitch, float roll);

// `p` through `cam`. False for a point at or behind the eye. `depth` is the
// perspective factor there - 1 at the origin's depth, more nearer the eye.
bool vecProject(const VecCamera *cam, Vec3 p, SDL_FPoint *out, float *depth);

// A glowing line in screen space. `width` is the core's, in logical pixels;
// the halo is three times that. Lines collect until vecFlush().
void vecLine(float x0, float y0, float x1, float y1, float width,
             SDL_Color color, float alpha);

// The same through a camera: thinner and, with fog, dimmer with distance.
void vecLine3(const VecCamera *cam, Vec3 a, Vec3 b, float width,
              SDL_Color color, float alpha);

// An outline of `sides` straight edges round (cx, cy).
void vecPolygon(float cx, float cy, float radius, int sides, float angle,
                float width, SDL_Color color, float alpha);

// Draws everything collected since the last flush, additively.
void vecFlush(void);

// `a` and `b` mixed, `t` of the way to `b`.
SDL_Color mixColor(SDL_Color a, SDL_Color b, float t);
