#include "game-state.h"
#include "../screens/game-over-screen.h"
#include "../screens/intro-screen.h"
#include "../screens/level-select-screen.h"
#include "../screens/menu-screen.h"
#include "../screens/playing-screen.h"
#include "../screens/win-screen.h"
#include <stdbool.h>

GameState nextGameState;

static GameState gameState;
static bool restartRequested;

GameState getGameState(void)
{
  return gameState;
}

void requestGameStateRestart(void)
{
  restartRequested = true;
}

void changeGameStateUpdate(void)
{
  if (gameState == nextGameState && !restartRequested)
  {
    return;
  }

  restartRequested = false;

  destroyGameState();

  gameState = nextGameState;

  initializeGameState();
}

void initializeGameState(void)
{
  switch (gameState)
  {
  case GAME_STATE_INTRO_SCREEN:
    initializeIntroScreen();
    break;
  case GAME_STATE_MENU_SCREEN:
    initializeMenuScreen();
    break;
  case GAME_STATE_LEVEL_SELECT_SCREEN:
    initializeLevelSelectScreen();
    break;
  case GAME_STATE_PLAYING_SCREEN:
    initializePlaying();
    break;
  case GAME_STATE_GAME_OVER_SCREEN:
    initializeGameOverScreen();
    break;
  case GAME_STATE_WIN_SCREEN:
    initializeWinScreen();
    break;
  }
}

void updateGameState(void)
{
  switch (gameState)
  {
  case GAME_STATE_INTRO_SCREEN:
    updateIntroScreen();
    break;
  case GAME_STATE_MENU_SCREEN:
    updateMenuScreen();
    break;
  case GAME_STATE_LEVEL_SELECT_SCREEN:
    updateLevelSelectScreen();
    break;
  case GAME_STATE_PLAYING_SCREEN:
    updatePlaying();
    break;
  case GAME_STATE_GAME_OVER_SCREEN:
    updateGameOverScreen();
    break;
  case GAME_STATE_WIN_SCREEN:
    updateWinScreen();
    break;
  }
}

void drawGameState(void)
{
  switch (gameState)
  {
  case GAME_STATE_INTRO_SCREEN:
    drawIntroScreen();
    break;
  case GAME_STATE_MENU_SCREEN:
    drawMenuScreen();
    break;
  case GAME_STATE_LEVEL_SELECT_SCREEN:
    drawLevelSelectScreen();
    break;
  case GAME_STATE_PLAYING_SCREEN:
    drawPlaying();
    break;
  case GAME_STATE_GAME_OVER_SCREEN:
    drawGameOverScreen();
    break;
  case GAME_STATE_WIN_SCREEN:
    drawWinScreen();
    break;
  }
}

void destroyGameState(void)
{
  switch (gameState)
  {
  case GAME_STATE_INTRO_SCREEN:
    destroyIntroScreen();
    break;
  case GAME_STATE_MENU_SCREEN:
    destroyMenuScreen();
    break;
  case GAME_STATE_LEVEL_SELECT_SCREEN:
    destroyLevelSelectScreen();
    break;
  case GAME_STATE_PLAYING_SCREEN:
    destroyPlaying();
    break;
  case GAME_STATE_GAME_OVER_SCREEN:
    destroyGameOverScreen();
    break;
  case GAME_STATE_WIN_SCREEN:
    destroyWinScreen();
    break;
  }
}
