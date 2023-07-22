#pragma once

// The name and version of this game, in the one place that owns them.
//
// Three packaging scripts need these strings — the .app's Info.plist, the
// Windows payload's README, the Linux one's — and the game itself needs two of
// them to find its save file. Written down in each of those, they would be four
// copies of a version number, and the one that goes stale is the one nobody was
// looking at. The scripts read these lines with sed, so keep them one per line
// and keep the value in double quotes.

#define BREAKUP_VERSION "1.0.0"

// What the player sees: the window title, the .app, the archive names.
#define BREAKUP_APP_NAME "Breakup"

// Where settings, progress and high scores live:
// SDL_GetPrefPath(BREAKUP_SAVE_ORG, BREAKUP_SAVE_APP).
//
// **These are not the display name and changing either one abandons every save
// already on disk.** The game shipped as "Breakup" out of a directory called
// "Breakup" and that is now a fact about other people's machines rather than a
// naming decision still open to us. src/lib/save.c had them written into the
// call; they are here because the Windows and Linux READMEs are the only place a
// player is told which folder to delete, and a wrong path there is worse than no
// path at all.
#define BREAKUP_SAVE_ORG "RobertLib"
#define BREAKUP_SAVE_APP "Breakup"

// macOS only, and the identity Gatekeeper and the notary service know the app by.
#define BREAKUP_BUNDLE_ID "com.robertlib.breakup"
