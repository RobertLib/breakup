#pragma once

#include "../globals.h"

// The end of a run, however it ended: dying, abandoning it, cashing out or
// winning. It reads what it shows from runLastResult(), which runFinish() has
// already recorded in the save by the time this screen comes up.
void initializeRunOverScreen(void);

void updateRunOverScreen(void);

void drawRunOverScreen(void);

void destroyRunOverScreen(void);
