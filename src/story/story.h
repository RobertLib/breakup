#pragma once

#include "../globals.h"
#include "../run/run.h"

// The Sovereign's scenes: the being the run is fought against, a face of lines
// that drifts over a tunnel of them and writes what it has to say a letter at
// a time, in a voice made of sung vowels.
//
// There are four of them. The prologue opens a run - the whole of the story
// the first time, a line of greeting after that - and ends on the first act
// and a map of the run ahead. An interlude comes between every two stages: a
// line about the stage coming up, the fight after it, the curse just taken or
// the one just beaten, over the same map with the next stage lit on it. The
// finale is the core going, and the face with it. The epilogue is a last word
// over the result, when a run ends by dying or by cashing out.
//
// A scene is drawn over whichever screen asked for it - the playing screen for
// the first three, the run-over screen for the last - and while it is up the
// host runs nothing but it, the way the playing screen runs nothing but the
// perk draft while that is open. SPACE, ENTER or a click writes the page out or
// turns it; ESC skips the scene. OPTIONS > STORY switches all of it off.

// The run about to begin opens with the prologue. The menu and the run-over
// screen ask for it as they start one; a development helper that starts a run
// does not, so that a capture of a level is a capture of the level.
void storyRequestPrologue(void);

// A run has begun: forget what the last one was told, and play the prologue if
// it was asked for. initializePlaying() calls it once the paddle is set up.
void storyRunBegan(void);

// Between two stages, once the run has moved on to the next one. `cleared` is
// the level index of the stage just finished.
void storyPlayInterlude(int cleared);

// The Sovereign has fallen. `done` runs when the scene is over - at once, if
// the story is switched off.
void storyPlayFinale(void (*done)(void));

// A run has ended in play, by dying or by cashing out. Asked for where that
// happens, so that the run-over screen can tell an ending from a preview.
void storyRequestEpilogue(void);

// The run-over screen, on its way in. Plays the epilogue if one was asked for
// and the Sovereign has something to say about `result`, and runs `done` when
// it is over; false, having done nothing, otherwise.
bool storyPlayEpilogue(const RunResult *result, void (*done)(void));

bool storyActive(void);

// True while the scene is opaque, so that the host need not draw under it.
bool storyCoversScreen(void);

void updateStory(void);

// Draws into whatever is the render target, which is the host's scene pass:
// the lines are meant to bloom.
void drawStory(void);

// The host is going away under the scene. Drops it without running `done`.
void storyStop(void);

// Releases the glyphs, once, at shutdown.
void destroyStory(void);

// Development helper: BREAKUP_STORY=prologue|interlude|finale|epilogue opens
// that scene straight away (see src/main.c).
void storyDevRequest(const char *scene);
