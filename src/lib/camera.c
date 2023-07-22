#include "camera.h"
#include "../paddle/paddle.h"

// How long a full-strength shake takes to die away, how fast the frame is
// knocked about while it does, and how far it travels at trauma 1.
#define TRAUMA_DECAY 1.7f
#define SHAKE_FREQUENCY 26.0f
#define SHAKE_MAX_OFFSET 14.0f
#define SHAKE_MAX_ANGLE 1.15f
#define PUNCH_DECAY 4.5f

Camera camera;

bool isCameraMoving(void)
{
  return camera.y != camera.prevY;
}

void addTrauma(float amount)
{
  camera.trauma = fminf(camera.trauma + amount, 1.0f);
}

void addPunch(float amount)
{
  camera.punch = fminf(camera.punch + amount, 1.0f);
}

float shakeAmount(void)
{
  return camera.trauma * camera.trauma;
}

float cameraPunch(void)
{
  return camera.punch;
}

// One hashed value per integer step of the noise, in [-1, 1].
static float hashNoise(int i, int seed)
{
  unsigned int h = (unsigned int)i * 374761393u + (unsigned int)seed * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  h ^= h >> 16;

  return (float)(h & 0xffffu) / 32767.5f - 1.0f;
}

// Value noise: the hashed steps above, smoothstepped between. Three channels
// out of one function, each on its own seed, so the two axes and the roll never
// move in step with one another.
static float valueNoise(float t, int seed)
{
  int i = (int)floorf(t);
  float f = t - (float)i;
  float s = f * f * (3.0f - 2.0f * f);

  return lerp(hashNoise(i, seed), hashNoise(i + 1, seed), s);
}

void cameraShake(float *outX, float *outY, float *outAngle)
{
  float shake = shakeAmount();

  if (shake <= 0.0005f)
  {
    *outX = 0;
    *outY = 0;
    *outAngle = 0;
    return;
  }

  float t = camera.shakeTime * SHAKE_FREQUENCY;

  *outX = SHAKE_MAX_OFFSET * shake * valueNoise(t, 1);
  *outY = SHAKE_MAX_OFFSET * shake * valueNoise(t, 2);
  *outAngle = SHAKE_MAX_ANGLE * shake * valueNoise(t, 3);
}

void initializeCamera(void)
{
  // Position the camera so the paddle sits near the bottom of the screen
  float startY = paddle.pos.y + PADDLE_HEIGHT + 30.0f - SCREEN_HEIGHT;
  camera.y = startY < 0.0f ? 0.0f : startY;
  camera.prevY = camera.y;
  camera.trauma = 0;
  camera.punch = 0;
  camera.shakeTime = 0;
}

void updateCamera(void)
{
  if (camera.y <= 0)
  {
    camera.y = 0;
    camera.prevY = 0;
  }

  // Both decay linearly, and the noise is advanced by real time rather than by
  // the scaled time the world runs on - a hit stop should hold the frame still,
  // not slow the rattle down to a wobble.
  camera.shakeTime += (float)realDt;
  camera.trauma = fmaxf(0.0f, camera.trauma - TRAUMA_DECAY * (float)realDt);
  camera.punch = fmaxf(0.0f, camera.punch - PUNCH_DECAY * (float)realDt);
}
