#pragma once

#include "../globals.h"

// The store page's cover, staged rather than played: see cover-screen.c. Only
// BREAKUP_STATE=cover reaches it, and nothing in the game links to it.

void initializeCoverScreen(void);

void updateCoverScreen(void);

void drawCoverScreen(void);

void destroyCoverScreen(void);
