#pragma once

#include "../globals.h"

// Parallax space background: tiled nebula, an animated geometry layer and two
// live star layers.
// worldIndex 0..3 selects a world theme, WORLD_COUNT (4) selects the menu theme.

void initializeStarfield(void);

// The variant the menus and the end screens ask for, as opposed to a level
#define BACKGROUND_MENU (-1)

// variant: which layout of the geometry layer to draw - the level number, so
//   that consecutive levels never come out looking the same, or BACKGROUND_MENU.
//   Changing it re-rolls the layer, and a given level always rolls the same one.
// scrollY: world scroll offset (pass camera.y in game, a slow drift in menus)
void drawBackground(int worldIndex, int variant, float scrollY);
