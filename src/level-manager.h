#pragma once

#include "globals.h"
#include "level-types.h"

// One level by 0-based index, and never NULL: an index outside 0..numLevels-1
// gets an empty level named LEVEL instead.
//
// paddle.level reaches the bricks, the enemies, the HUD and the game over
// screen, and it arrives from a save file, from BREAKUP_LEVEL and from the
// level select grid. Each of those used to index the levels array directly,
// so the one place a bad value could be caught was five places that did not -
// which is why the array itself is no longer handed out.
const Level *getLevel(int index);

// Get number of levels
int getNumberOfLevels(void);

// True when this level is a boss fight, which the HUD, the level banner and the
// music all ask before they say "WORLD n".
bool isBossLevel(int index);

// Which 1-based level this is inside its world, or 0 for a boss level - those
// have no number and the HUD says BOSS instead.
//
// This and worldForLevel() both count only the levels that are not boss
// fights, so the four worlds are still the twenty hand-designed levels five at
// a time whatever gets interleaved between them, and a boss belongs to the
// world it interrupts. Dividing the raw index by five stopped being the answer
// the moment anything was inserted.
int levelInWorld(int index);

// Get the Y pixel position of the bottom of the last non-empty row for a given level
int getLevelContentBottom(int levelIndex);

// Initialize level manager (loads all level txt files)
void initializeLevelManager(void);

// Cleanup level manager
void destroyLevelManager(void);
