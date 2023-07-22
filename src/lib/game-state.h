#pragma once

typedef enum GameState
{
  GAME_STATE_INTRO_SCREEN,
  GAME_STATE_MENU_SCREEN,
  GAME_STATE_LEVEL_SELECT_SCREEN,
  GAME_STATE_PLAYING_SCREEN,
  GAME_STATE_GAME_OVER_SCREEN,
  GAME_STATE_WIN_SCREEN,
} GameState;

extern GameState nextGameState;

// The state actually being drawn right now, which is not always nextGameState:
// the two differ for the frame between asking for a change and getting it.
GameState getGameState(void);

// Re-initializes the current state on the next frame (e.g. level restart)
void requestGameStateRestart(void);

void changeGameStateUpdate(void);

void initializeGameState(void);

void updateGameState(void);

void drawGameState(void);

void destroyGameState(void);
