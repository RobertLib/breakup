#pragma once

#include "../globals.h"

// The last eight seconds of play, kept so that a moment worth showing somebody
// can be saved after it has happened: G writes them out as a GIF.
//
// Only the playing screen is recorded - the field and the HUD, not a pause
// menu or the cards between two stages of a run - at half size and fifteen
// frames a second, into a ring that the next eight seconds overwrite. Saving hands that
// ring to a thread - the palette and the LZW are most of a second of work - and
// recording waits until it is done, so what gets written is exactly what was
// on screen when the key went down.
//
// Where it goes: a "Breakup" folder in the player's Pictures, or the game's own
// data folder when there is no Pictures to be had. The browser build has no
// file for a player to find afterwards, so it has none of this (see
// BREAKUP_OFFERS_CLIPS in globals.h).

// Recording at all; main.c turns it off for a scripted run, which has its own
// way of taking pictures and must not spend time on this one.
void clipSetEnabled(bool enabled);

// Takes a frame if one is due. The playing screen calls it on every frame
// worth keeping, straight after it has drawn its HUD: the frame is the scene
// endScene() just composited, recomposited at half size, with `overlay` drawn
// over it the way it was drawn over the window.
void clipCaptureFrame(void (*overlay)(void));

// Reads back the frame clipCaptureFrame() composited last time round. Called
// at the top of every frame, before anything is drawn - see the note inside.
void clipBeginFrame(void);

// Starts writing the clip. Says what happened in a toast either way.
void clipSave(void);

// Picks up a finished save and reports it. Called once a frame.
void clipUpdate(void);

// Waits for a save in progress and frees the ring.
void clipShutdown(void);
