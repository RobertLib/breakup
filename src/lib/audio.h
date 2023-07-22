#pragma once

#include "../globals.h"
#include "../types.h"

// Sound effects are synthesized at startup; the music is nineteen Ogg Vorbis
// files, embedded in the binary by embed_assets.sh. Neither needs anything on
// disk.

typedef enum Sfx
{
  SFX_BALL_PADDLE,
  SFX_BALL_WALL,
  SFX_BRICK_HIT,   // brick damaged but not destroyed
  SFX_BRICK_BREAK,
  SFX_EXPLOSION,
  SFX_GOLD,
  SFX_CRYSTAL,
  SFX_POWERUP_GOOD,
  SFX_POWERUP_BAD,
  SFX_SHOOT,
  SFX_ENEMY_HIT,
  SFX_LIFE_LOST,
  SFX_LEVEL_COMPLETE,
  SFX_MENU_MOVE,
  SFX_MENU_SELECT,
  SFX_SHIELD,
  SFX_CATCH,
  SFX_GAME_OVER, // jingle
  SFX_WIN,       // jingle
  SFX_COUNT
} Sfx;

// Four per world against LEVELS_PER_WORLD of five, so a world plays a, b, c, d
// and only comes back to a on its last level. Two was a track every other
// level, which is three hearings of the first one in a world that takes a
// quarter of an hour.
#define GAME_TRACKS_PER_WORLD 4

// Shared out across the boss fights by their `#boss N` number. There are more
// fights than themes, so this only thins the repetition rather than ending it.
#define BOSS_TRACK_COUNT 2

typedef enum MusicTrack
{
  MUSIC_NONE = -1,
  MUSIC_MENU,
  MUSIC_GAME_FIRST,
  // GAME_TRACKS_PER_WORLD tracks per world follow MUSIC_GAME_FIRST, then
  // BOSS_TRACK_COUNT boss themes follow those
  MUSIC_BOSS_FIRST = MUSIC_GAME_FIRST + WORLD_COUNT * GAME_TRACKS_PER_WORLD,
  MUSIC_COUNT = MUSIC_BOSS_FIRST + BOSS_TRACK_COUNT
} MusicTrack;

// Each world has its own set of tracks and its levels walk through them,
// counting within the world rather than across the whole game so that the boss
// fights in between do not skew which track a level lands on. A boss level
// gets a boss theme, picked by which boss it is.
MusicTrack musicForLevel(int level);

// Loads the sound effects and the music. The music tracks are not decoded here
// - they are handed to SDL_mixer as the encoded bytes they are embedded as, and
// decoded a buffer at a time while they play.
void initializeAudio(void);

void destroyAudio(void);

void playSfx(Sfx sfx);

// The same sound, resampled up or down. `semitones` is musical rather than a
// raw ratio because that is how a rising combo wants to be heard: a chain of
// brick breaks walking up a scale reads as one climbing phrase, where the same
// sound repeated at one pitch is a rattle. 0 is the sound as synthesized.
void playSfxAt(Sfx sfx, float semitones);

// Starts a looping music track; no-op if it is already playing
void playMusic(MusicTrack track);

void stopMusic(void);

void setMusicVolume(float v); // 0..1
void setSfxVolume(float v);   // 0..1
