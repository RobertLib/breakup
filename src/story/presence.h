#pragma once

#include "../globals.h"

// The Sovereign while the game is being played, rather than between the
// levels of it.
//
// On an ordinary level it is a face in the back of the field, faint enough to
// read the bricks through and bright enough in the eyes to be felt: it watches
// the ball, and it reacts. A crystal breaking is a thought it loses, and it
// flinches, and its face slips; a life lost is something it enjoys. On the
// last boss of the game it is not in the back of anything - it is the core of
// the thing, and every hit on the crystal is a hit on it, until the face comes
// apart with it.
//
// It is part of the story, and OPTIONS > STORY switches it off with the rest.

typedef enum PresenceEvent
{
  PRESENCE_CRYSTAL,   // a crystal broke - one of its thoughts
  PRESENCE_CORE_HIT,  // its own core, on The Sovereign's level, took a hit
  PRESENCE_LIFE_LOST,
  PRESENCE_CLEARED,   // the level is over
} PresenceEvent;

// A level has been built: works out whether it watches this one, and from
// where, and scans itself in.
void presenceBegin(void);

void presenceReact(PresenceEvent event);

void updatePresence(void);

// Behind the bricks: the face in the back of an ordinary level.
void drawPresence(void);

// Over the boss: the face on The Sovereign's core.
void drawPresenceOnCore(void);
