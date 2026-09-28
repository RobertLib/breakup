#pragma once

#include "../globals.h"

// The perks a run offers between its levels, and what unlocks them.
//
// Each one is a rule that hooks into a system the game already has - the
// combo, the capsules, the explosions, the fireball - rather than a system of
// its own, because a perk is only interesting when it makes two things that
// already exist start talking to each other. CHAIN REACTION on its own is a
// small bonus; next to CRYSTAL BLAST and INFERNO it is a level that clears
// itself, and finding that is the point.
//
// The order is the save file's: saveData.perksUnlocked is a bitmask indexed by
// this enum, so a new perk goes on the end, before PERK_EXTRA_LIFE.
typedef enum Perk
{
  // Unlocked from the start
  PERK_GOLD_FEVER,
  PERK_MAGNET,
  PERK_LUCKY,
  PERK_OVERTIME,
  PERK_SAFETY_NET,
  PERK_SOFT_HANDS,
  PERK_CRYSTAL_BLAST,
  PERK_TRACER,

  // Earned by playing, in the campaign or in a run
  PERK_CHAIN_REACTION,
  PERK_HUNTER,
  PERK_SPLIT_SHOT,
  PERK_GLASS_CANNON,
  PERK_INFERNO,
  PERK_MELTDOWN,

  // Taken and spent on the spot rather than kept for the run
  PERK_EXTRA_LIFE,

  PERK_COUNT
} Perk;

#define PERK_BIT(perk) (1u << (unsigned)(perk))

typedef struct PerkInfo
{
  const char *name;
  const char *description;
  const char *unlockHint; // NULL for the perks a first run already offers
  bool instant;           // spent when it is taken, never held
  SDL_Color color;
} PerkInfo;

// Never NULL: an out-of-range perk gets a blank entry rather than a crash.
const PerkInfo *perkInfo(Perk perk);

// The bits every save starts with, and the bits a save may hold at all.
Uint32 starterPerks(void);
Uint32 unlockablePerks(void);

bool perkUnlocked(Perk perk);

// How many of the unlockable perks the save has; for the menu's "9/14".
int unlockedPerkCount(void);
int perkCollectionSize(void);

// What the game reports as it is played, so that a perk can unlock the moment
// its condition is met. They count within one level where the condition says
// "in one level", and they run in the campaign as well as in a run - beating
// The Gyre is beating The Gyre wherever it happens.
void noteLevelStarted(void);
void noteDetonation(void);
void noteEnemyKilled(void);
void noteComboMultiplier(int multiplier);
void noteBossDefeated(int bossNumber);
