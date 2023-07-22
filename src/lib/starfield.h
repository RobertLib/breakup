#pragma once

#include "../globals.h"

// Parallax space background: tiled nebula + two live star layers.
// worldIndex 0..3 selects a world theme, WORLD_COUNT (4) selects the menu theme.

void initializeStarfield(void);

// scrollY: world scroll offset (pass camera.y in game, a slow drift in menus)
void drawBackground(int worldIndex, float scrollY);
