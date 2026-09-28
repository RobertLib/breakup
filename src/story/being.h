#pragma once

#include "../globals.h"
#include "../lib/vector.h"
#include "script.h"

// The Sovereign's face: drawn in lines, in perspective, with a halo turning
// behind it. It drifts about the spot it is asked to hold, turns its head and
// its eyes towards whatever it is looking at, and opens its mouth on every
// syllable it speaks. Each mood moves the brows, the eyes and the mouth; the
// more of its thoughts a run has cost it, the worse the picture holds together.
//
// It is a value rather than a singleton because it is in more than one place
// at once: the scene it speaks in, the title it writes, the menu it looms over,
// the back of every level, where it watches the ball, and the core of the last
// boss, which is the one place it can be hurt.

#define BEING_MAX_SEGS 640

typedef struct BeingShard
{
  float dx0, dy0, dx1, dy1;
  float x, y, vx, vy;
  float angle, spin;
  float width, alpha;
  SDL_Color color;
} BeingShard;

// What a mood does to the face. The brows are offsets in face units - the
// inner end, the outer end, and a lift of the left brow alone for the one
// that is amused - `smile` bends the corners of the mouth, `eyeOpen` is how
// wide the lids are, `mouthRest` holds the mouth open between syllables, and
// `jitter` shakes the head.
typedef struct BeingFace
{
  float browInner, browOuter, browLift;
  float smile, eyeOpen, mouthRest, jitter;
  float warmth; // how far the colour leans to red
} BeingFace;

typedef struct Being
{
  float x, y, vx, vy;
  float targetX, targetY;
  float lookX, lookY;
  float yaw, pitch, roll;

  float scale;   // pixels to a unit of face; the face is about 2.5 units tall
  float eyeGlow; // how much brighter the glows are than the lines
  float wander;  // how far it drifts about its target, 1 as in a scene
  float turn;    // how far it turns its head to look, 1 as in a scene
  float grid;    // how much of the mask's lattice is drawn, 1 as in a scene
  bool halo;     // the rings behind the head and the shards round it

  float mouth;
  float blinkPhase; // 0..1 through a blink, or past 1 between them
  float blinkWait;
  float visible;
  bool wantVisible;
  float glitch;
  StoryMood mood;
  BeingFace face;
  float time;
  Uint32 rng; // its own, so that a face in the back of a level draws nothing
              // from the rand() the level's drops and particles come out of

  // A band of the face knocked sideways, held for a few frames at a time.
  float bandY, bandH, bandShift, bandHold;

  bool shattered;
  float shatterT;
  int shardCount;
  BeingShard shards[BEING_MAX_SEGS];

  // Worked out as it is drawn: where the glows go, and where the writing
  // beam leaves from.
  Vec3 pupilAt[2];
  Vec3 thirdEyeAt;
  Vec3 mouthAt;
  float pupilOpen;
  SDL_FPoint penOrigin;
} Being;

// Off the top of the screen, invisible, and about to scan itself in on its
// way down to the middle: how it arrives in a scene.
void beingReset(Being *b);

// Puts it at (x, y) and keeps it there, with no swoop in.
void beingPlace(Being *b, float x, float y);

void beingSetMood(Being *b, StoryMood mood);

// 0 is whole, 1 is barely holding together: lines flicker out, bands of the
// face slip sideways, and a second image drifts off the first.
void beingSetGlitch(Being *b, float glitch);

// One syllable. The mouth opens by `strength` and closes again on its own.
void beingSpeak(Being *b, float strength);

// Where it drifts to and where it looks, in screen space.
void beingSetTarget(Being *b, float x, float y);
void beingLookAt(Being *b, float x, float y);

// Draws it in (towards true) or out, as a scan line sweeping up or down the
// face.
void beingSetVisible(Being *b, bool visible);

// The face comes apart into its lines, which fly outwards and go out. After
// this it draws nothing but the pieces.
void beingShatter(Being *b);

void updateBeing(Being *b, float dt);

// `alpha` fades the whole of it: a scene fading over a level, or a face in
// the back of one that should be felt more than seen.
void drawBeing(Being *b, SDL_Color tint, float alpha);
