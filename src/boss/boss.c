#include "boss.h"
#include "../balls/balls.h"
#include "../bricks/brick-item.h"
#include "../bricks/bricks.h"
#include "../enemies/enemies.h"
#include "../level-manager.h"
#include "../level-types.h"
#include "../lib/audio.h"
#include "../lib/camera.h"
#include "../lib/effects.h"
#include "../lib/gfx.h"
#include "../lib/particles.h"
#include "../paddle/paddle.h"
#include "../types.h"
#include "../ui/floating-text.h"

#define CORE_RADIUS 34.0f
#define ARMOR_RADIUS 19.0f

// How long the crystal stays shut after a hit. Without it a multiball would
// take a whole fight off in one pass and the ring would never get to close.
#define CORE_INVULN 0.7f

#define DEATH_DURATION 2.0f

// How many enemies a summoning boss keeps on the field at once. The pool holds
// twenty-four, and THE BROOD at full pace asks for two every one and a bit
// seconds - left uncapped it fills the pool inside a phase, and twenty-four
// things coming down at a paddle is not a fight any more, it is a wall.
#define BOSS_MINION_CAP 5

// How the thing travels the field.
typedef enum BossMove
{
  MOVE_SWEEP,     // side to side, with a shallow bob
  MOVE_LISSAJOUS, // a slow figure of eight
  MOVE_STALK,     // sweeps, and sinks toward the paddle and back as it goes
} BossMove;

// What it does to you when it gets the chance.
typedef enum BossAttack
{
  ATTACK_AIMED,  // single shots at the paddle, out of the gaps in its own ring
  ATTACK_SPREAD, // a fan of three, under itself
  ATTACK_RADIAL, // a ring of shots, in every direction at once
} BossAttack;

typedef struct BossRing
{
  int plates;   // 0 means this ring is not there
  float orbit;  // distance from the crystal
  float spin;   // rad/s, and the sign is the direction
} BossRing;

typedef struct BossDef
{
  int hp;      // crystal hits to take it down, three per phase or so
  int plateHp; // hits to break one plate
  BossRing ring[BOSS_MAX_RINGS];
  float sweep;      // how far it travels either side of centre
  float homeY;      // where it sits, in a single-screen arena
  float dive;       // how far it sinks, for MOVE_STALK
  float orbitPulse; // how much the rings breathe, 0 for not at all
  BossMove move;
  BossAttack attack;
  float attackPeriod;
  int minions;    // enemies it puts on the field per attack, from phase 1
  SDL_Color tint; // the armour's colour; the crystal keeps the phase colours
} BossDef;

// The seven, in the order they are met.
//
// `homeY` and the orbits are not free: a ring has to clear the boss bar at the
// top of the field (y 54..64) at the highest point of whatever path it travels,
// and stay off the arena bricks at row 15 (y 398) at the lowest. A breathing
// ring is measured at its widest and a sweeping one at the top of its bob.
// THE BROOD sits lowest because its figure of eight carries it 58px either way;
// THE PULSAR sits low because its ring opens out to 117.
//
// Each one is a different question. The Warden is the tutorial: one slow ring,
// shots you can see coming. The Gyre spins hard the other way and its plates
// take three. The Brood barely has a ring and fills the field instead. The
// Binary puts a second ring inside the first, turning against it, so a gap is
// two gaps that have to line up. The Pulsar breathes its ring in and out and
// fires in every direction at once. The Stalker comes down after you. The
// Sovereign does all of it.
static const BossDef bossDefs[] = {
    // 1 - THE WARDEN
    {6, 2, {{8, 88.0f, 0.55f}, {0, 0, 0}}, 210.0f, 210.0f, 0, 0,
     MOVE_SWEEP, ATTACK_AIMED, 2.8f, 0, {190, 215, 255, 255}},

    // 2 - THE GYRE
    {7, 3, {{6, 92.0f, -1.25f}, {0, 0, 0}}, 235.0f, 215.0f, 0, 0.12f,
     MOVE_SWEEP, ATTACK_SPREAD, 2.5f, 0, {140, 255, 205, 255}},

    // 3 - THE BROOD
    {7, 2, {{5, 84.0f, 0.7f}, {0, 0, 0}}, 200.0f, 230.0f, 0, 0,
     MOVE_LISSAJOUS, ATTACK_AIMED, 2.2f, 2, {170, 255, 130, 255}},

    // 4 - THE BINARY
    {8, 2, {{7, 96.0f, 0.6f}, {4, 58.0f, -1.1f}}, 215.0f, 210.0f, 0, 0,
     MOVE_SWEEP, ATTACK_AIMED, 2.4f, 1, {255, 205, 130, 255}},

    // 5 - THE PULSAR
    {8, 2, {{9, 90.0f, 0.8f}, {0, 0, 0}}, 190.0f, 230.0f, 0, 0.3f,
     MOVE_SWEEP, ATTACK_RADIAL, 2.8f, 0, {255, 175, 105, 255}},

    // 6 - THE STALKER
    {9, 3, {{7, 88.0f, -0.9f}, {0, 0, 0}}, 245.0f, 195.0f, 86.0f, 0,
     MOVE_STALK, ATTACK_SPREAD, 2.0f, 1, {225, 160, 255, 255}},

    // 7 - THE SOVEREIGN
    {10, 3, {{8, 92.0f, 1.0f}, {5, 60.0f, -1.4f}}, 225.0f, 205.0f, 60.0f, 0.15f,
     MOVE_STALK, ATTACK_RADIAL, 1.8f, 2, {255, 235, 255, 255}},
};

#define BOSS_DEF_COUNT ((int)(sizeof(bossDefs) / sizeof(bossDefs[0])))

Boss boss;

static const BossDef *def(void)
{
  return &bossDefs[clamp(boss.def, 0, BOSS_DEF_COUNT - 1)];
}

// Everything gets faster as the crystal loses, and this is the one number that
// says so. It is the phase in a single multiplier rather than three tables.
static float pace(void)
{
  static const float byPhase[3] = {1.0f, 1.35f, 1.8f};
  return byPhase[clamp(boss.phase, 0, 2)];
}

static SDL_Color coreColor(void)
{
  // Cools from white-hot to red as it loses, so the phase can be read off the
  // crystal and not only off the bar at the top. Every boss shares this - it is
  // the language of the fight, where the armour's colour is just which boss.
  switch (boss.phase)
  {
  case 0:
    return (SDL_Color){150, 235, 255, 255};
  case 1:
    return (SDL_Color){255, 200, 110, 255};
  default:
    return (SDL_Color){255, 110, 120, 255};
  }
}

// Phase 0 is the first third of the health, and so on: the ring closes at each
// boundary, which is what makes a fight three fights rather than one long one.
static int phaseForHp(int hp)
{
  int max = boss.maxHp;

  if (hp > (max * 2) / 3)
  {
    return 0;
  }

  return hp > max / 3 ? 1 : 2;
}

static float ringOrbit(int ring)
{
  return def()->ring[ring].orbit *
         (1.0f + def()->orbitPulse * sinf(boss.pulseT));
}

// World-space centre of one plate, for whichever way its ring is facing now.
static Vec2 armorPos(const BossArmor *plate)
{
  float a = plate->angle + boss.spin[plate->ring];
  float r = ringOrbit(plate->ring);

  return (Vec2){boss.pos.x + cosf(a) * r, boss.pos.y + sinf(a) * r};
}

// Puts every plate back up to full health.
//
// `grow` is what separates the two callers. At a phase change the ring coming
// back is the loud moment of the fight, so a plate that was gone grows in from
// nothing and the crystal stays shut for as long as that takes. At the start of
// the level there is nothing to announce and nobody watching for it - the boss
// arrives wearing its armour - so the ring is simply there.
static void closeRing(bool grow)
{
  for (int i = 0; i < boss.armorCount; i++)
  {
    if (!boss.armor[i].active)
    {
      boss.armor[i].active = true;
      boss.armor[i].respawnT = grow ? 0.0f : 1.0f;
    }

    boss.armor[i].hp = def()->plateHp;
    boss.armor[i].hitFlash = 0;
  }
}

void initializeBoss(void)
{
  boss = (Boss){0};

  const Level *level = getLevel(paddle.level);

  if (level->boss <= 0)
  {
    return;
  }

  boss.present = true;
  boss.alive = true;
  boss.def = clamp(level->boss - 1, 0, BOSS_DEF_COUNT - 1);
  boss.hp = def()->hp;
  boss.maxHp = def()->hp;
  boss.phase = 0;
  boss.pos = (Vec2){SCREEN_WIDTH / 2.0f, def()->homeY};
  boss.attackTimer = 3.0f;

  for (int r = 0; r < BOSS_MAX_RINGS; r++)
  {
    int plates = def()->ring[r].plates;

    for (int i = 0; i < plates && boss.armorCount < BOSS_ARMOR_MAX; i++)
    {
      BossArmor *plate = &boss.armor[boss.armorCount++];

      plate->ring = r;
      plate->angle = (float)i / plates * 2.0f * SDL_PI_F;
    }
  }

  // Already up, rather than growing in: closeRing() owns respawnT, and setting
  // it here was a line that read like the intent and did nothing, because the
  // plate is still inactive at this point and the call below overwrote it.
  closeRing(false);
}

bool bossAlive(void)
{
  return boss.present && boss.alive;
}

// When a ring closes, whatever is inside it is thrown out.
//
// A ball that gets in through a gap has earned every hit it lands before the
// phase turns, and a ball rattling around in there is the best thing that can
// happen to the player. A ball sealed *inside* a fresh ring is a different
// thing: it cannot be reached, it cannot get out, and it would grind the rest
// of the fight down on its own while the player watched.
static void ejectBallsFromRing(void)
{
  float outer = ringOrbit(0);

  for (int i = 0; i < BALL_COUNT; i++)
  {
    Ball *ball = &balls[i];

    if (!ball->active || isBallDocked(ball))
    {
      continue;
    }

    float cx = ball->pos.x + BALL_SIZE / 2.0f;
    float cy = ball->pos.y + BALL_SIZE / 2.0f;
    float dx = cx - boss.pos.x;
    float dy = cy - boss.pos.y;
    float dist = sqrtf(dx * dx + dy * dy);

    if (dist > outer)
    {
      continue;
    }

    // Thrown straight down when it is dead centre, rather than in whatever
    // direction a division by zero would have picked.
    Vec2 out = dist > 0.001f ? (Vec2){dx / dist, dy / dist} : (Vec2){0, 1};
    float clear = outer + ARMOR_RADIUS + BALL_SIZE;

    ball->pos.x = boss.pos.x + out.x * clear - BALL_SIZE / 2.0f;
    ball->pos.y = boss.pos.y + out.y * clear - BALL_SIZE / 2.0f;
    ball->vel = vec2Norm(out, ball->speed);

    spawnGlowPuff(cx, cy, (SDL_Color){255, 200, 120, 255}, 70, 0.3f);
  }
}

static void breakPlate(BossArmor *plate, Vec2 at)
{
  plate->active = false;

  addScore(150);
  playSfxAt(SFX_BRICK_BREAK, -3.0f);
  spawnBurst(at.x, at.y, def()->tint, 16, 250);
  spawnGlowPuff(at.x, at.y, def()->tint, 70, 0.35f);
  addTrauma(0.18f);

  // The arena is thin, so a plate is most of what can drop anything on a boss
  // level. The rate is well above a brick's: a fight this long with no
  // multiball and no expand in it is a war of attrition.
  if (frand() < 0.4f)
  {
    trySpawnBrickItem(at.x - GFX_ITEM_W / 2.0f, at.y);
  }
}

// One hit on a plate, from whatever landed it.
//
// `feedback` is the tink and the spark for a hit the plate survived, and it is
// the caller's to decide because the three sources want different answers: a
// ball asks for it every time, an explosion never does (it takes out a whole
// neighbourhood at once and has a bang of its own - four plates clacking
// underneath it is noise), and the laser asks for it on a cooldown. A plate that
// actually breaks is loud whatever this says: breakPlate() is its own event.
static void damagePlate(BossArmor *plate, Vec2 at, bool feedback)
{
  plate->hp--;

  if (plate->hp <= 0)
  {
    breakPlate(plate, at);
    return;
  }

  plate->hitFlash = 1.0f;

  if (feedback)
  {
    playSfx(SFX_BALL_WALL);
    spawnGlowPuff(at.x, at.y, (SDL_Color){200, 220, 255, 255}, 34, 0.2f);
  }
}

// Whether a plate is up and solid enough to be hit. A plate growing back at a
// phase change is not: it would otherwise take a hit before it looks like
// anything, which reads as the ball bouncing off nothing.
static bool plateIsSolid(const BossArmor *plate)
{
  return plate->active && plate->respawnT >= 0.35f;
}

static void hitCore(Vec2 at)
{
  boss.hp--;
  boss.coreFlash = 1.0f;
  boss.invuln = CORE_INVULN;

  addScore(500);

  SDL_Color color = coreColor();

  playSfx(SFX_CRYSTAL);
  spawnBurst(at.x, at.y, color, 30, 320);
  spawnGlowPuff(boss.pos.x, boss.pos.y, color, 170, 0.5f);
  spawnFloatingText(boss.pos.x, boss.pos.y - 50, "HIT!", color);

  addTrauma(0.55f);
  addPunch(0.6f);
  addHitstop(0.11f);
  addFlash(color, 0.5f);

  if (boss.hp <= 0)
  {
    boss.hp = 0;
    boss.alive = false;
    boss.dyingT = 0;

    addScore(3000);
    spawnFloatingText(boss.pos.x, boss.pos.y - 80, "CORE DOWN",
                      (SDL_Color){255, 255, 255, 255});

    addTrauma(1.0f);
    addHitstop(0.22f);
    clearEnemyBullets();
    return;
  }

  int phase = phaseForHp(boss.hp);

  if (phase != boss.phase)
  {
    // The ring comes back, and everything it does gets faster. This is the one
    // moment in a fight that takes ground back from the player, so it is loud:
    // the armour grows in rather than appearing, and the crystal stays shut for
    // as long as that takes.
    boss.phase = phase;
    boss.invuln = fmaxf(boss.invuln, 1.2f);

    closeRing(true);
    ejectBallsFromRing();

    playSfx(SFX_LEVEL_COMPLETE);
    spawnFloatingText(boss.pos.x, boss.pos.y + 60, "ARMOUR RESTORED",
                      (SDL_Color){255, 170, 90, 255});
    addFlash((SDL_Color){255, 180, 120, 255}, 0.7f);
    addTrauma(0.7f);
  }
}

// True when `vel` points into a surface whose outward normal is `normal`.
static bool approaching(Vec2 vel, Vec2 normal)
{
  return vel.x * normal.x + vel.y * normal.y < 0;
}

bool bossCollide(float cx, float cy, float radius, Vec2 vel,
                 Vec2 *outNormal, float *outPush)
{
  if (!bossAlive())
  {
    return false;
  }

  // The plates first: one of them standing between the ball and the crystal is
  // the whole point of them, and testing the crystal first would let a ball
  // score through a plate it was touching.
  for (int i = 0; i < boss.armorCount; i++)
  {
    BossArmor *plate = &boss.armor[i];

    if (!plateIsSolid(plate))
    {
      continue;
    }

    Vec2 at = armorPos(plate);
    float dx = cx - at.x;
    float dy = cy - at.y;
    float reach = radius + ARMOR_RADIUS;
    float distSq = dx * dx + dy * dy;

    if (distSq > reach * reach)
    {
      continue;
    }

    Vec2 normal = vec2Norm((Vec2){dx, dy}, 1.0f);

    if (!approaching(vel, normal))
    {
      continue;
    }

    *outNormal = normal;
    *outPush = reach - sqrtf(distSq);

    damagePlate(plate, at, true);

    return true;
  }

  float dx = cx - boss.pos.x;
  float dy = cy - boss.pos.y;
  float reach = radius + CORE_RADIUS;
  float distSq = dx * dx + dy * dy;

  if (distSq > reach * reach)
  {
    return false;
  }

  Vec2 normal = vec2Norm((Vec2){dx, dy}, 1.0f);

  if (!approaching(vel, normal))
  {
    return false;
  }

  *outNormal = normal;
  *outPush = reach - sqrtf(distSq);

  if (boss.invuln > 0)
  {
    // Shut: it bounces off and says so, rather than silently doing nothing.
    playSfx(SFX_SHIELD);
    spawnGlowPuff(cx, cy, (SDL_Color){180, 200, 255, 255}, 50, 0.25f);
    return true;
  }

  hitCore((Vec2){cx, cy});

  return true;
}

void damageBossArmorAt(float x, float y, float radius)
{
  if (!bossAlive())
  {
    return;
  }

  for (int i = 0; i < boss.armorCount; i++)
  {
    BossArmor *plate = &boss.armor[i];

    if (!plateIsSolid(plate))
    {
      continue;
    }

    Vec2 at = armorPos(plate);
    float dx = at.x - x;
    float dy = at.y - y;
    float reach = radius + ARMOR_RADIUS;

    if (dx * dx + dy * dy <= reach * reach)
    {
      damagePlate(plate, at, false);
    }
  }
}

// How often a bolt is allowed to make a noise. The laser fires a pair every
// 0.45s and a bolt that lands on the crystal does nothing at all, so a player
// holding the button down with the ring stripped was getting four "shut" chimes
// a second forever. The hit still happens every time; only the sound is spaced
// out enough to stay information rather than becoming a tone.
#define BULLET_SFX_INTERVAL 0.2f

bool bossAbsorbsBullet(float cx, float cy, float radius)
{
  if (!bossAlive())
  {
    return false;
  }

  bool sound = boss.bulletSfxCooldown <= 0;

  // Armour only, and the nearest plate takes the whole bolt.
  for (int i = 0; i < boss.armorCount; i++)
  {
    BossArmor *plate = &boss.armor[i];

    if (!plateIsSolid(plate))
    {
      continue;
    }

    Vec2 at = armorPos(plate);
    float dx = cx - at.x;
    float dy = cy - at.y;
    float reach = radius + ARMOR_RADIUS;

    if (dx * dx + dy * dy > reach * reach)
    {
      continue;
    }

    // A plate that breaks is loud whatever the cooldown says - breakPlate() is
    // its own event, and it is the one the player is shooting for.
    damagePlate(plate, at, sound);

    boss.bulletSfxCooldown = BULLET_SFX_INTERVAL;

    return true;
  }

  // The crystal stops a bolt and takes nothing from it - see the note in
  // boss.h for why the laser is not a way in.
  float dx = cx - boss.pos.x;
  float dy = cy - boss.pos.y;
  float reach = radius + CORE_RADIUS;

  if (dx * dx + dy * dy > reach * reach)
  {
    return false;
  }

  if (sound)
  {
    playSfx(SFX_SHIELD);
    spawnGlowPuff(cx, cy, (SDL_Color){180, 200, 255, 255}, 44, 0.22f);
    boss.bulletSfxCooldown = BULLET_SFX_INTERVAL;
  }

  return true;
}

// The death sequence: explosions walking outward from the crystal for a couple
// of seconds, and only then is the level over. A boss that simply vanished on
// the last hit would end its level on the quietest frame in it.
static void updateDying(void)
{
  float before = boss.dyingT;
  boss.dyingT += (float)dt;

  // Roughly eight bursts across the sequence, at a steady pace rather than one
  // per frame, so it reads as a series of detonations.
  const float interval = DEATH_DURATION / 8.0f;

  if (floorf(boss.dyingT / interval) > floorf(before / interval))
  {
    float spread = 40.0f + 80.0f * (boss.dyingT / DEATH_DURATION);
    float ax = boss.pos.x + frandRange(-spread, spread);
    float ay = boss.pos.y + frandRange(-spread, spread);

    playSfx(SFX_EXPLOSION);
    spawnBurst(ax, ay, (SDL_Color){255, 170, 80, 255}, 24, 300);
    spawnGlowPuff(ax, ay, (SDL_Color){255, 200, 110, 255}, 150, 0.45f);
    addTrauma(0.45f);
    addFlash((SDL_Color){255, 190, 120, 255}, 0.5f);
  }

  if (before < DEATH_DURATION && boss.dyingT >= DEATH_DURATION)
  {
    for (int i = 0; i < boss.armorCount; i++)
    {
      boss.armor[i].active = false;
    }

    killEnemiesInRadius(SCREEN_WIDTH / 2.0f, boss.pos.y, 4000.0f);
    clearEnemyBullets();

    // The crystal going up takes the arena with it. It is the right picture,
    // and it is what keeps the boss the thing that ends the level: the bricks
    // here are cover and a source of power-ups, and a couple of them left
    // standing in a corner should not outlive the fight and leave the player
    // mopping up. Steel shrugs this off exactly as it shrugs off everything.
    for (int i = 0; i < numBricks; i++)
    {
      damageBrick(&bricks[i], 99, false, (Vec2){0, 0});
    }

    addFlash((SDL_Color){255, 255, 255, 255}, 1.0f);
    addTrauma(1.0f);
    checkLevelComplete();
  }
}

// Shots out of the gaps the player made, aimed at the paddle. The armour is
// cover for the boss as well as a wall for them, so stripping it opens the
// crystal and the gun at the same time.
static void attackAimed(void)
{
  int fired = 0;
  int wanted = boss.phase + 1;

  for (int i = 0; i < boss.armorCount && fired < wanted; i++)
  {
    if (boss.armor[i].active || boss.armor[i].ring != 0)
    {
      continue;
    }

    Vec2 at = armorPos(&boss.armor[i]);

    if (at.y < boss.pos.y)
    {
      continue; // pointing away from the player
    }

    spawnEnemyBullet(at.x, at.y);
    spawnGlowPuff(at.x, at.y, (SDL_Color){255, 120, 170, 255}, 40, 0.2f);
    fired++;
  }

  // Sealed in, so it shoots from underneath itself instead.
  if (fired == 0)
  {
    spawnEnemyBullet(boss.pos.x, boss.pos.y + CORE_RADIUS);
  }
}

static void attackSpread(void)
{
  float y = boss.pos.y + CORE_RADIUS;
  int arms = 3 + boss.phase;

  for (int i = 0; i < arms; i++)
  {
    float t = arms > 1 ? (float)i / (arms - 1) * 2.0f - 1.0f : 0;
    spawnEnemyBulletAt(boss.pos.x, y, t * 150.0f, 210.0f);
  }

  spawnGlowPuff(boss.pos.x, y, (SDL_Color){255, 120, 170, 255}, 60, 0.25f);
}

static void attackRadial(void)
{
  int arms = 6 + boss.phase * 2;

  for (int i = 0; i < arms; i++)
  {
    float a = (float)i / arms * 2.0f * SDL_PI_F + boss.spin[0];
    spawnEnemyBulletAt(boss.pos.x + cosf(a) * CORE_RADIUS,
                       boss.pos.y + sinf(a) * CORE_RADIUS,
                       cosf(a) * 185.0f, sinf(a) * 185.0f);
  }

  spawnGlowPuff(boss.pos.x, boss.pos.y, (SDL_Color){255, 140, 190, 255}, 120, 0.3f);
  addTrauma(0.2f);
}

static void attack(void)
{
  // One shot sound for the volley, whatever shape it is and however many
  // bullets it puts on the field. See spawnEnemyBullet() in enemies.h.
  playSfx(SFX_SHOOT);

  switch (def()->attack)
  {
  case ATTACK_SPREAD:
    attackSpread();
    break;
  case ATTACK_RADIAL:
    attackRadial();
    break;
  default:
    attackAimed();
    break;
  }

  // From the second phase on the ones that summon put something on the field,
  // so the player cannot camp under a gap and wait.
  if (boss.phase >= 1)
  {
    int live = 0;

    for (int i = 0; i < MAX_ENEMIES; i++)
    {
      if (enemies[i].active)
      {
        live++;
      }
    }

    int room = BOSS_MINION_CAP - live;

    for (int i = 0; i < def()->minions && i < room; i++)
    {
      EnemyType type = boss.phase >= 2 ? ENEMY_DIVER : ENEMY_DRIFTER;
      spawnEnemyAt(type, boss.pos.x + frandRange(-120, 120), boss.pos.y + 90);
    }
  }
}

static void moveBoss(void)
{
  float t = boss.moveT;
  float cx = SCREEN_WIDTH / 2.0f;

  switch (def()->move)
  {
  case MOVE_LISSAJOUS:
    boss.pos.x = cx + sinf(t) * def()->sweep;
    boss.pos.y = def()->homeY + sinf(t * 2.0f) * 58.0f;
    break;
  case MOVE_STALK:
    // Sinks most of the way down the field and climbs back, on a longer cycle
    // than the sweep, so where it will be next is a question the player has to
    // keep asking.
    boss.pos.x = cx + sinf(t) * def()->sweep;
    boss.pos.y = def()->homeY +
                 (0.5f - 0.5f * cosf(t * 0.62f)) * def()->dive;
    break;
  default:
    boss.pos.x = cx + sinf(t) * def()->sweep;
    boss.pos.y = def()->homeY + sinf(t * 1.7f) * 22.0f;
    break;
  }
}

void updateBoss(void)
{
  if (!boss.present)
  {
    return;
  }

  for (int i = 0; i < boss.armorCount; i++)
  {
    BossArmor *plate = &boss.armor[i];
    plate->hitFlash = fmaxf(0.0f, plate->hitFlash - 4.0f * (float)dt);

    if (plate->active && plate->respawnT < 1.0f)
    {
      plate->respawnT = fminf(1.0f, plate->respawnT + 1.6f * (float)dt);
    }
  }

  boss.coreFlash = fmaxf(0.0f, boss.coreFlash - 2.5f * (float)dt);

  // Above the `!boss.alive` branch, so it keeps draining while the thing is
  // going up - the player can still be firing into it.
  boss.bulletSfxCooldown = fmaxf(0.0f, boss.bulletSfxCooldown - (float)dt);

  if (!boss.alive)
  {
    updateDying();
    return;
  }

  float speed = pace();

  boss.invuln = fmaxf(0.0f, boss.invuln - (float)dt);
  boss.moveT += 0.42f * speed * (float)dt;
  boss.pulseT += 1.1f * speed * (float)dt;

  for (int r = 0; r < BOSS_MAX_RINGS; r++)
  {
    boss.spin[r] += def()->ring[r].spin * speed * (float)dt;
  }

  moveBoss();

  // Nothing happens until the player is actually holding a ball.
  if (!paddle.isReady || paddle.type == PADDLE_TYPE_DYING)
  {
    return;
  }

  boss.attackTimer -= (float)dt;

  if (boss.attackTimer <= 0)
  {
    boss.attackTimer = def()->attackPeriod / speed;
    attack();
  }
}

void drawBoss(void)
{
  if (!boss.present)
  {
    return;
  }

  float screenY = boss.pos.y - camera.y;
  SDL_Color color = coreColor();
  bool gone = !boss.alive && boss.dyingT >= DEATH_DURATION;

  if (!gone)
  {
    // A faint disc out to the widest ring, so the shape of the armour is
    // legible even where a plate has gone.
    SDL_SetTextureColorMod(texGlow, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(texGlow, 40);
    float ringSize = (ringOrbit(0) + ARMOR_RADIUS) * 2.2f;
    SDL_FRect ring = {boss.pos.x - ringSize / 2, screenY - ringSize / 2,
                      ringSize, ringSize};
    SDL_RenderTexture(renderer, texGlow, NULL, &ring);
    SDL_SetTextureColorMod(texGlow, 255, 255, 255);
    SDL_SetTextureAlphaMod(texGlow, 255);
  }

  // Armour plates, drawn as the bricks they are meant to look like: a plate
  // lies along its ring, so it turns with it. The inner ring is drawn smaller,
  // which is what keeps two rings from reading as one thick one.
  for (int i = 0; i < boss.armorCount; i++)
  {
    const BossArmor *plate = &boss.armor[i];

    if (!plate->active)
    {
      continue;
    }

    Vec2 at = armorPos(plate);
    float grow = easeOutBack(clamp(plate->respawnT, 0.0f, 1.0f));
    float scale = plate->ring == 0 ? 1.0f : 0.78f;
    float w = 46.0f * grow * scale;
    float h = 20.0f * grow * scale;
    float deg = (plate->angle + boss.spin[plate->ring]) * 57.2958f + 90.0f;

    SDL_Color tint = def()->tint;
    float wear = plate->hp > 1 ? 1.0f : 0.72f;

    if (plate->hitFlash > 0)
    {
      wear = 1.0f;
    }

    SDL_SetTextureColorMod(texBrick,
                           (Uint8)(tint.r * wear),
                           (Uint8)(tint.g * wear),
                           (Uint8)(tint.b * wear));
    SDL_SetTextureAlphaMod(texBrick,
                           (Uint8)(255 * clamp(plate->respawnT, 0.0f, 1.0f)));

    SDL_FRect dst = {at.x - w / 2, at.y - camera.y - h / 2, w, h};
    SDL_RenderTextureRotated(renderer, texBrick, NULL, &dst, deg, NULL,
                             SDL_FLIP_NONE);

    SDL_SetTextureColorMod(texBrick, 255, 255, 255);
    SDL_SetTextureAlphaMod(texBrick, 255);
  }

  if (gone)
  {
    return;
  }

  // The crystal. It pulses faster the closer it is to going, and when it is
  // shut it is dimmed rather than hidden - the player has to be able to see
  // that it is there and not yet worth hitting.
  float urgency = 1.0f + 2.0f * (1.0f - boss.hp / (float)boss.maxHp);
  float pulse = 0.85f + 0.15f * sinf(gameTime * 4.0f * urgency);
  float shut = boss.invuln > 0 ? 0.45f : 1.0f;
  float size = CORE_RADIUS * 2.0f * pulse * (1.0f + 0.25f * boss.coreFlash);

  SDL_SetTextureColorMod(texGlow, color.r, color.g, color.b);
  SDL_SetTextureAlphaMod(texGlow, (Uint8)((150 + 105 * boss.coreFlash) * shut));
  float glowSize = size * 2.4f;
  SDL_FRect glow = {boss.pos.x - glowSize / 2, screenY - glowSize / 2,
                    glowSize, glowSize};
  SDL_RenderTexture(renderer, texGlow, NULL, &glow);
  SDL_SetTextureColorMod(texGlow, 255, 255, 255);
  SDL_SetTextureAlphaMod(texGlow, 255);

  Uint8 flash = (Uint8)(255 * boss.coreFlash);
  SDL_SetTextureColorMod(texCrystal,
                         (Uint8)clamp(color.r * shut + flash, 0, 255),
                         (Uint8)clamp(color.g * shut + flash, 0, 255),
                         (Uint8)clamp(color.b * shut + flash, 0, 255));

  SDL_FRect core = {boss.pos.x - size / 2, screenY - size / 2, size, size};
  SDL_RenderTextureRotated(renderer, texCrystal, NULL, &core,
                           sinf(gameTime * 0.8f) * 12.0f, NULL, SDL_FLIP_NONE);
  SDL_SetTextureColorMod(texCrystal, 255, 255, 255);
}

void drawBossBar(void)
{
  if (!boss.present)
  {
    return;
  }

  const float w = 420.0f;
  const float h = 10.0f;
  float x = SCREEN_WIDTH / 2.0f - w / 2;
  float y = 54.0f;

  float frac = clamp(boss.hp / (float)boss.maxHp, 0.0f, 1.0f);
  SDL_Color color = coreColor();

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

  SDL_SetRenderDrawColor(renderer, 10, 12, 28, 220);
  SDL_FRect bg = {x, y, w, h};
  SDL_RenderFillRect(renderer, &bg);

  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 255);
  SDL_FRect fill = {x, y, w * frac, h};
  SDL_RenderFillRect(renderer, &fill);

  // The two phase boundaries, so the player can see the next wall coming.
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
  for (int i = 1; i < 3; i++)
  {
    SDL_FRect tick = {x + w * (i / 3.0f) - 1, y, 2, h};
    SDL_RenderFillRect(renderer, &tick);
  }

  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 150);
  SDL_RenderRect(renderer, &bg);
}
