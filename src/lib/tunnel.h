#pragma once

#include "../globals.h"
#include "vector.h"

// The Sovereign's backdrop: a tunnel of slowly turning hexagons flowing out of
// the distance, with stars streaking past down it, all in vector lines. It is
// what its scenes are spoken over, and the menus and the end of a run draw it
// too, so that the game around the story looks like the same place.

#define TUNNEL_STARS 90

typedef struct TunnelStar
{
  Vec3 p;
  float speed;
} TunnelStar;

typedef struct Tunnel
{
  TunnelStar stars[TUNNEL_STARS];
  float time;
} Tunnel;

void tunnelReset(Tunnel *t);

void updateTunnel(Tunnel *t, float dt);

// Down the tunnel from (cx, cy), which is where its far end is. Everything it
// draws goes through the vector batch, and is flushed.
void drawTunnel(const Tunnel *t, SDL_Color tint, float alpha, float cx, float cy);
