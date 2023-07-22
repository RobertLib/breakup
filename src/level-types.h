#pragma once

// For STATUS_BAR_HEIGHT, which LEVEL_PATTERN_TOP below is measured from.
#include "globals.h"

#define LEVEL_PATTERN_COLS 15
#define LEVEL_PATTERN_ROWS 150
#define LEVEL_PATTERN_LENGTH (LEVEL_PATTERN_COLS * LEVEL_PATTERN_ROWS)
#define LEVEL_PATTERN_INDENT 11
#define LEVEL_PATTERN_SPACING 2
#define LEVEL_PATTERN_ROW_HEIGHT 20
// Vertical start of the pattern: the status bar plus one empty brick row, so
// the top of a level never sits flush against the HUD
#define LEVEL_PATTERN_TOP (STATUS_BAR_HEIGHT + LEVEL_PATTERN_ROW_HEIGHT + LEVEL_PATTERN_SPACING)
#define LEVEL_HEIGHT (LEVEL_PATTERN_TOP + LEVEL_PATTERN_ROWS * (LEVEL_PATTERN_ROW_HEIGHT + LEVEL_PATTERN_SPACING))

#define LEVEL_NAME_MAX 24

// Pattern cell characters:
//   _ or space  empty
//   B  basic brick (1 hit)
//   D  durable brick (2 hits)
//   T  tough brick (3 hits)
//   S  solid brick (indestructible)
//   X  explosive brick (blows up its neighborhood)
//   G  gold brick (bonus points)
//   F  final crystal (destroy all of these to win the level)
//   E  drifter enemy spawn point
//   V  diver enemy spawn point
//   W  splitter enemy spawn point
//   U  shooter (saucer) enemy spawn point
// A `#boss N` line anywhere in the file makes the level a boss fight: N picks
// which one, and 0 (the default) means the level is an ordinary one. The bricks
// of a boss level are the arena rather than the objective - it ends when the
// boss does.
typedef struct Level
{
  char pattern[LEVEL_PATTERN_LENGTH];
  char name[LEVEL_NAME_MAX];
  int boss;
} Level;
