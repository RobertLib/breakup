#include "game-state.h"
#include "../screens/cover-screen.h"
#include "../screens/intro-screen.h"
#include "../screens/menu-screen.h"
#include "../screens/playing-screen.h"
#include "../screens/run-over-screen.h"
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
  case GAME_STATE_PLAYING_SCREEN:
    initializePlaying();
    break;
  case GAME_STATE_RUN_OVER_SCREEN:
    initializeRunOverScreen();
    break;
  case GAME_STATE_COVER_SCREEN:
    initializeCoverScreen();
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
  case GAME_STATE_PLAYING_SCREEN:
    updatePlaying();
    break;
  case GAME_STATE_RUN_OVER_SCREEN:
    updateRunOverScreen();
    break;
  case GAME_STATE_COVER_SCREEN:
    updateCoverScreen();
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
  case GAME_STATE_PLAYING_SCREEN:
    drawPlaying();
    break;
  case GAME_STATE_RUN_OVER_SCREEN:
    drawRunOverScreen();
    break;
  case GAME_STATE_COVER_SCREEN:
    drawCoverScreen();
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
  case GAME_STATE_PLAYING_SCREEN:
    destroyPlaying();
    break;
  case GAME_STATE_RUN_OVER_SCREEN:
    destroyRunOverScreen();
    break;
  case GAME_STATE_COVER_SCREEN:
    destroyCoverScreen();
    break;
  }
}
