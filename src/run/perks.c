#include "perks.h"
#include "../lib/audio.h"
#include "../lib/save.h"
#include "../ui/toast.h"

// Thresholds for the unlocks that count something within one level.
#define CHAIN_UNLOCK_DETONATIONS 8
#define HUNTER_UNLOCK_KILLS 10
#define SPLIT_UNLOCK_MULTIPLIER 5

// The bosses whose defeat unlocks a perk, by their `#boss N` number - which is
// the entry in bossDefs[] they fight with, so it names the fight rather than
// where a level file happens to put it.
#define BOSS_GYRE 2
#define BOSS_PULSAR 5
#define BOSS_SOVEREIGN 7

#define COLOR_SCORE {255, 215, 90, 255}
#define COLOR_CAPSULE {120, 255, 160, 255}
#define COLOR_SURVIVAL {110, 220, 255, 255}
#define COLOR_BLAST {255, 150, 70, 255}
#define COLOR_COMBO {255, 120, 200, 255}

static const PerkInfo perks[PERK_COUNT] = {
    [PERK_GOLD_FEVER] = {"GOLD FEVER",
                         "GOLD BRICKS ALWAYS DROP A CAPSULE.",
                         NULL, false, COLOR_CAPSULE},
    [PERK_MAGNET] = {"MAGNET",
                     "FALLING CAPSULES DRIFT TOWARDS THE PADDLE.",
                     NULL, false, COLOR_CAPSULE},
    [PERK_LUCKY] = {"LUCKY",
                    "BRICKS DROP CAPSULES 50% MORE OFTEN.",
                    NULL, false, COLOR_CAPSULE},
    [PERK_OVERTIME] = {"OVERTIME",
                       "EVERY POWER-UP LASTS 50% LONGER.",
                       NULL, false, COLOR_SURVIVAL},
    [PERK_SAFETY_NET] = {"SAFETY NET",
                         "THE FIRST BALL YOU LOSE IN EACH LEVEL BOUNCES BACK.",
                         NULL, false, COLOR_SURVIVAL},
    [PERK_SOFT_HANDS] = {"SOFT HANDS",
                         "A BALL CAUGHT BY THE CATCH PADDLE KEEPS THE COMBO GOING.",
                         NULL, false, COLOR_COMBO},
    [PERK_CRYSTAL_BLAST] = {"CRYSTAL BLAST",
                            "CRYSTALS EXPLODE WHEN THEY BREAK.",
                            NULL, false, COLOR_BLAST},
    [PERK_TRACER] = {"TRACER ROUNDS",
                     "LASER HITS SCORE AT THE COMBO MULTIPLIER AND BUILD THE COMBO.",
                     NULL, false, COLOR_COMBO},
    [PERK_CHAIN_REACTION] = {"CHAIN REACTION",
                             "EVERY BRICK AN EXPLOSION BREAKS ADDS ONE TO THE COMBO.",
                             "DETONATE 8 EXPLOSIVE BRICKS IN ONE LEVEL",
                             false, COLOR_BLAST},
    [PERK_HUNTER] = {"HUNTER",
                     "EVERY ENEMY YOU KILL ADDS THREE TO THE COMBO.",
                     "KILL 10 ENEMIES IN ONE LEVEL",
                     false, COLOR_COMBO},
    [PERK_SPLIT_SHOT] = {"SPLIT SHOT",
                         "EVERY STEP UP THE COMBO LAUNCHES AN EXTRA BALL.",
                         "REACH A COMBO OF X5",
                         false, COLOR_COMBO},
    [PERK_GLASS_CANNON] = {"GLASS CANNON",
                           "THE COMBO GOES UP TO X8 INSTEAD OF X5, BUT BALLS ARE 15% FASTER.",
                           "BEAT THE GYRE",
                           false, COLOR_SCORE},
    [PERK_INFERNO] = {"INFERNO",
                      "WHILE FIREBALL IS LIT, EXPLOSIONS ARE 75% WIDER.",
                      "BEAT THE PULSAR",
                      false, COLOR_BLAST},
    [PERK_MELTDOWN] = {"MELTDOWN",
                       "FIREBALL MELTS STEEL BRICKS.",
                       "BEAT THE SOVEREIGN",
                       false, COLOR_BLAST},
    [PERK_EXTRA_LIFE] = {"+1 LIFE",
                         "ONE MORE LIFE, RIGHT NOW.",
                         NULL, true, COLOR_SURVIVAL},
};

// A missing initializer would be a zeroed entry with a NULL name, which is a
// crash the first time a card is drawn rather than a compile error. The enum
// and the table have to grow together; see the same guard in brick-item.c.
_Static_assert(SDL_arraysize(perks) == PERK_COUNT,
               "perks[] must hold exactly one entry per Perk");

static const PerkInfo blankPerk = {"?", "", NULL, true, {255, 255, 255, 255}};

static int detonationsThisLevel;
static int killsThisLevel;

const PerkInfo *perkInfo(Perk perk)
{
  if ((unsigned)perk >= (unsigned)PERK_COUNT || perks[perk].name == NULL)
  {
    return &blankPerk;
  }

  return &perks[perk];
}

Uint32 starterPerks(void)
{
  Uint32 mask = 0;

  for (int i = 0; i < PERK_COUNT; i++)
  {
    if (!perks[i].instant && perks[i].unlockHint == NULL)
    {
      mask |= PERK_BIT(i);
    }
  }

  return mask;
}

Uint32 unlockablePerks(void)
{
  Uint32 mask = 0;

  for (int i = 0; i < PERK_COUNT; i++)
  {
    if (!perks[i].instant)
    {
      mask |= PERK_BIT(i);
    }
  }

  return mask;
}

bool perkUnlocked(Perk perk)
{
  if ((unsigned)perk >= (unsigned)PERK_COUNT)
  {
    return false;
  }

  if (perks[perk].instant)
  {
    return true;
  }

  return (saveData.perksUnlocked & PERK_BIT(perk)) != 0;
}

int unlockedPerkCount(void)
{
  int count = 0;

  for (int i = 0; i < PERK_COUNT; i++)
  {
    if (!perks[i].instant && perkUnlocked((Perk)i))
    {
      count++;
    }
  }

  return count;
}

int perkCollectionSize(void)
{
  int count = 0;

  for (int i = 0; i < PERK_COUNT; i++)
  {
    if (!perks[i].instant)
    {
      count++;
    }
  }

  return count;
}

static void unlockPerk(Perk perk)
{
  if (perkUnlocked(perk))
  {
    return;
  }

  saveData.perksUnlocked |= PERK_BIT(perk);
  writeSave();

  char text[64];
  snprintf(text, sizeof(text), "PERK UNLOCKED: %s", perks[perk].name);
  pushToast(text, perks[perk].color);
  playSfx(SFX_POWERUP_GOOD);
}

void noteLevelStarted(void)
{
  detonationsThisLevel = 0;
  killsThisLevel = 0;
}

void noteDetonation(void)
{
  if (++detonationsThisLevel >= CHAIN_UNLOCK_DETONATIONS)
  {
    unlockPerk(PERK_CHAIN_REACTION);
  }
}

void noteEnemyKilled(void)
{
  if (++killsThisLevel >= HUNTER_UNLOCK_KILLS)
  {
    unlockPerk(PERK_HUNTER);
  }
}

void noteComboMultiplier(int multiplier)
{
  if (multiplier >= SPLIT_UNLOCK_MULTIPLIER)
  {
    unlockPerk(PERK_SPLIT_SHOT);
  }
}

void noteBossDefeated(int bossNumber)
{
  switch (bossNumber)
  {
  case BOSS_GYRE:
    unlockPerk(PERK_GLASS_CANNON);
    break;
  case BOSS_PULSAR:
    unlockPerk(PERK_INFERNO);
    break;
  case BOSS_SOVEREIGN:
    unlockPerk(PERK_MELTDOWN);
    break;
  default:
    break;
  }
}
