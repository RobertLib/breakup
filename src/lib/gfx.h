#pragma once

#include "../globals.h"
#include "../types.h"

// All game sprites are generated procedurally at startup.
// White (grayscale) textures are tinted at draw time via SDL_SetTextureColorMod.
//
// Sprites are generated at GFX_SS x their logical size and drawn at logical
// size, so they stay sharp on high-DPI displays and in fullscreen.

#define GFX_SS 3

#define GFX_BRICK_W 50
#define GFX_BRICK_H 20
#define GFX_PADDLE_H 20
#define GFX_BALL_SIZE 20
#define GFX_GLOW_SIZE 128
#define GFX_ENEMY_SIZE 32
#define GFX_ENEMY_MINI_SIZE 18
#define GFX_ITEM_W 40
#define GFX_ITEM_H 16

// Bricks
extern SDL_Texture *texBrick;      // white, tintable
extern SDL_Texture *texBrickSolid; // baked metallic
extern SDL_Texture *texBrickGold;  // baked gold
extern SDL_Texture *texCrystal;    // white crystal, tintable
extern SDL_Texture *texBombEmblem; // overlay for explosive bricks
extern SDL_Texture *texCrackLight; // damage overlay stage 1
extern SDL_Texture *texCrackHeavy; // damage overlay stage 2

// Ball & effects
extern SDL_Texture *texBall;   // white sphere, tintable
extern SDL_Texture *texGlow;   // radial glow, additive
extern SDL_Texture *texSquare; // 8x8 white square (particle shards)

// Background elements
extern SDL_Texture *texStar;    // small additive star dot
extern SDL_Texture *texSparkle; // 4-point star flare, additive
extern SDL_Texture *texFlow;    // tileable noise wisp layer, additive

// Paddle (index 0 = default 100px, 1 = long 150px, 2 = short 60px)
extern SDL_Texture *texPaddle[3];
extern SDL_Texture *texTurret;

// Enemies, indexed by EnemyType
extern SDL_Texture *texEnemy[ENEMY_TYPE_COUNT];

// Power-up capsules, indexed by ItemType
extern SDL_Texture *texItem[ITEM_COUNT];

// Nebula backgrounds: worlds 0..3, index 4 = menu. Tileable vertically.
extern SDL_Texture *texNebula[WORLD_COUNT + 1];

// HSV (h in degrees) to SDL_Color
SDL_Color hsvColor(float h, float s, float v);

// Base color of a power-up capsule (for glows around it)
SDL_Color itemColor(ItemType type);

// Cinematic edge darkening over the whole frame
void drawVignette(void);

void initializeGfx(void);

void destroyGfx(void);
