#pragma once

#include "../globals.h"

// The choice between two stages of a run, drawn over the level just cleared.
//
// After an ordinary level it is the perk draft: up to three perks, one to
// keep. After a boss it is the cash-out first - end the run and keep the
// score, or go deeper under a new curse for a higher multiplier - and then,
// for a player who went deeper, the draft. Choosing moves the run on to its
// next stage, so while this is up the playing screen runs nothing but it.

// Called when a run's level-complete banner has run its course.
void openRunOverlay(void);

bool runOverlayActive(void);

void updateRunOverlay(void);

void drawRunOverlay(void);

// Drops the overlay without choosing, when the playing screen goes away
// underneath it.
void destroyRunOverlay(void);
