#pragma once

#include "../globals.h"

// One-line notices at the top of the screen that outlive whichever screen
// raised them: a perk unlocked on the last hit of a boss is still being
// announced over the perk draft that follows, and a clip finishes writing
// wherever the player has gone in the meantime. main.c updates and draws
// them after everything else, on the wall clock, so a hit stop or a pause does
// not hold one on screen.
//
// Safe to call before the fonts exist and in the test runner, where there is
// no renderer: the notice is dropped rather than rasterized.
void pushToast(const char *text, SDL_Color color);

void updateToasts(void);

// `low` stacks them up from the bottom of the screen instead, for a screen
// whose own title sits where they would otherwise go.
void drawToasts(bool low);

void destroyToasts(void);
