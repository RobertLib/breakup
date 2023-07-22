#pragma once

#include "../globals.h"

// The boss fights.
//
// A level whose file carries a `#boss N` line builds one of these instead of
// ending when its bricks run out: the level is over when the boss is, and the
// bricks around it are the arena rather than the objective.
//
// A fight is the level's own vocabulary turned on the player. The boss is a
// crystal - the thing every other level asks you to break - behind a ring of
// armour plates that look like bricks and orbit it. A plate takes the hit a
// brick would and gives nothing for it; the crystal behind them is the only
// thing that counts, and the only way to it is a gap. Land enough hits and the
// ring closes again a little faster than before.
//
// `N` picks which of the seven, and they differ in everything the fight is
// made of: how many rings turn and which way, how the thing moves, what it
// shoots and whether it sends anything after you. bossDefs[] in boss.c is the
// whole of that - a new boss is an entry in a table and a level file, not code.

// Two rings at most, and enough plates for the widest of them.
#define BOSS_MAX_RINGS 2
#define BOSS_ARMOR_MAX 18

typedef struct BossArmor
{
  int ring;       // which ring carries it
  float angle;    // where on that ring, in radians, before the ring's own spin
  int hp;
  float hitFlash; // 1..0 white flash after a hit that did not break it
  float respawnT; // 0..1 grow-in when the ring closes at a phase change
  bool active;
} BossArmor;

typedef struct Boss
{
  bool present; // this level has a boss at all
  bool alive;
  int def; // index into bossDefs[]

  Vec2 pos; // world-space centre
  int hp;
  int maxHp;
  int phase; // 0, 1, 2 - the ring closes and the pace picks up at each step

  float moveT;                   // drives whatever path this one travels
  float spin[BOSS_MAX_RINGS];    // each ring's rotation, radians
  float pulseT;                  // drives rings that breathe in and out
  float coreFlash;               // 1..0 after a hit on the crystal
  float invuln;                  // seconds the crystal is closed to further hits
  float bulletSfxCooldown;       // rate limit on the laser's hit feedback
  float attackTimer;
  float dyingT; // counts up through the death sequence

  BossArmor armor[BOSS_ARMOR_MAX];
  int armorCount;
} Boss;

extern Boss boss;

// Builds the boss this level asks for, or marks that there is none.
void initializeBoss(void);

// True when this level has a boss and it is still up. The level cannot be
// completed while that is the case: on a boss level the bricks are the arena
// rather than the objective.
bool bossAlive(void);

// Tests a ball against the boss and applies what the hit does - armour damage,
// a crystal hit, the effects for either.
//
// `vel` is there so that only a ball travelling *into* a surface counts as
// hitting it. The boss moves, so a ball can find itself already overlapping one
// of these; without the test it would be registered as a fresh hit on every
// frame it took to get out, which on the crystal is the whole fight gone in a
// tenth of a second.
//
// Returns false when nothing was touched. Otherwise *outNormal is the unit
// surface normal to reflect off, and *outPush is how far along it the ball has
// to move to be clear of the surface.
bool bossCollide(float cx, float cy, float radius, Vec2 vel,
                 Vec2 *outNormal, float *outPush);

// Damage from a source that does not bounce (an explosion next door).
void damageBossArmorAt(float x, float y, float radius);

// A bolt from the paddle's laser, which is consumed by whatever it strikes
// rather than bounced off it. Returns true when it hit something, and the
// caller's only job then is to stop drawing the bolt.
//
// **Armour only.** The plates are bricks and the laser is the thing that breaks
// bricks, so stripping the ring is exactly what it should be good for - but the
// crystal behind them stays the ball's to reach, through a gap, because that is
// the whole shape of the fight. Twin bolts every 0.45s against a 0.7s core
// invulnerability is one hit on the crystal every 0.7s, which takes the longest
// boss in the game down in about seven seconds and turns every one of them into
// "hold space". A bolt that reaches the crystal is absorbed instead, with the
// same sound and flare the crystal gives a ball when it is shut - the player has
// to be able to see that it landed and did nothing.
bool bossAbsorbsBullet(float cx, float cy, float radius);

void updateBoss(void);

void drawBoss(void);

// The health bar across the top of the field, drawn with the rest of the HUD.
void drawBossBar(void);
