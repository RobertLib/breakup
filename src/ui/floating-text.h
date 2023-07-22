#pragma once

#include "../globals.h"

// Small world-space texts that rise and fade (score popups, power-up names).

// Clears what is on screen. The rasterized textures behind them are cached and
// reused across levels; destroyFloatingTexts() is what frees those, once, at
// shutdown and before the renderer goes.
void clearFloatingTexts(void);

void destroyFloatingTexts(void);

void spawnFloatingText(float x, float y, const char *text, SDL_Color color);

void updateFloatingTexts(void);

void drawFloatingTexts(void);
