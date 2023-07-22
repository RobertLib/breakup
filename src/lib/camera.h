#pragma once

#include "../globals.h"

typedef struct Camera
{
  float y;
  float prevY;

  // Impact shake, held as "trauma" in 0..1 rather than as a pixel count.
  // What gets drawn is trauma *squared*, so a graze barely registers while a
  // detonation throws the frame around - the response an arcade player expects
  // from a hit, and the one a linear magnitude does not give.
  float trauma;
  float shakeTime; // advances the noise the shake is sampled from
  float punch;     // 0..1 hit zoom, decays on its own
} Camera;

extern Camera camera;

bool isCameraMoving(void);

// Adds impact shake. `amount` is trauma in 0..1: about 0.2 for a small hit,
// 0.5 for an explosion, 0.8 for losing a life. It accumulates and is capped.
void addTrauma(float amount);

// Adds a short zoom-in punch, 0..1, for impacts that want weight without
// the frame rattling.
void addPunch(float amount);

// trauma squared - the shake strength everything visual is scaled by
float shakeAmount(void);

// Current shake displacement in logical pixels and roll in degrees. Sampled
// from smooth noise rather than drawn fresh each frame: a random number per
// frame is a buzz, and noise is a camera being knocked about.
void cameraShake(float *outX, float *outY, float *outAngle);

float cameraPunch(void);

void initializeCamera(void);

void updateCamera(void);
