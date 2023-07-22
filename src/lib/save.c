#include "save.h"
#include "../version.h"
#include "../level-manager.h"
#include <string.h>

SaveData saveData;

static char savePath[512];

// See setSaveScripted() in save.h for why this is here.
static bool scripted;

void setSaveScripted(bool value)
{
  scripted = value;
}

static void resolveSavePath(void)
{
  if (savePath[0] != '\0')
  {
    return;
  }

  char *pref = SDL_GetPrefPath(BREAKUP_SAVE_ORG, BREAKUP_SAVE_APP);

  if (pref != NULL)
  {
    snprintf(savePath, sizeof(savePath), "%ssave.txt", pref);
    SDL_free(pref);
  }
  else
  {
    snprintf(savePath, sizeof(savePath), "save.txt");
  }
}

void loadSave(void)
{
  // Defaults
  saveData.unlocked = 1;
  saveData.musicVol = 0.7f;
  saveData.sfxVol = 0.8f;
  saveData.fullscreen = false;

  for (int i = 0; i < HIGH_SCORE_COUNT; i++)
  {
    saveData.highScores[i] = 0;
    saveData.highLevels[i] = 0;
  }

  if (scripted)
  {
    return;
  }

  resolveSavePath();

  SDL_IOStream *file = SDL_IOFromFile(savePath, "r");
  if (file == NULL)
  {
    return;
  }

  Sint64 size = SDL_GetIOSize(file);
  if (size <= 0 || size > 8192)
  {
    SDL_CloseIO(file);
    return;
  }

  char buf[8193];
  size_t read = SDL_ReadIO(file, buf, (size_t)size);
  SDL_CloseIO(file);
  buf[read] = '\0';

  char *line = strtok(buf, "\n");
  while (line != NULL)
  {
    int iv;
    float fv;

    if (sscanf(line, "unlocked=%d", &iv) == 1)
    {
      // Clamped to the levels this build actually has, which is what
      // unlockLevel() below has always done - this end of it used a
      // hard-coded 100 (the level manager's own MAX_LEVELS, by coincidence
      // rather than by reference) because main() used to load the save before
      // the levels and there was no count to ask for yet. It loads them first
      // now.
      //
      // The guard is not decoration: clamp(n, 1, 0) is 0, and a build whose
      // assets did not embed would otherwise turn a perfectly good save into
      // zero unlocked levels and write it back out at exit.
      int total = getNumberOfLevels();

      saveData.unlocked = total > 0 ? clamp(iv, 1, total) : 1;
    }
    else if (sscanf(line, "music=%f", &fv) == 1)
    {
      saveData.musicVol = clamp(fv, 0.0f, 1.0f);
    }
    else if (sscanf(line, "sfx=%f", &fv) == 1)
    {
      saveData.sfxVol = clamp(fv, 0.0f, 1.0f);
    }
    else if (sscanf(line, "fullscreen=%d", &iv) == 1)
    {
      saveData.fullscreen = iv != 0;
    }
    else
    {
      for (int i = 0; i < HIGH_SCORE_COUNT; i++)
      {
        char key[16];
        snprintf(key, sizeof(key), "hs%d=%%d,%%d", i);
        int score, level;
        if (sscanf(line, key, &score, &level) == 2)
        {
          // Clamped for the same reason `unlocked` above is: this file is plain
          // text in the player's own directory and nothing stops it being
          // edited, truncated or written by an older build. recordScore() only
          // ever stores a positive score and a level that exists, so anything
          // else arriving here is not a score - and the one thing that must not
          // happen is the high score table printing "-00005" at somebody.
          int total = getNumberOfLevels();

          saveData.highScores[i] = score > 0 ? score : 0;
          saveData.highLevels[i] = total > 0 ? clamp(level, 0, total) : 0;
          break;
        }
      }
    }

    line = strtok(NULL, "\n");
  }
}

void writeSave(void)
{
  if (scripted)
  {
    return;
  }

  resolveSavePath();

  // The whole file is built before anything is opened. Opening it for writing
  // truncates it, so a failure between there and the write would leave the
  // player with an empty save rather than the one they had.
  char buf[1024];
  int len = snprintf(buf, sizeof(buf),
                     "unlocked=%d\nmusic=%.2f\nsfx=%.2f\nfullscreen=%d\n",
                     saveData.unlocked, saveData.musicVol, saveData.sfxVol,
                     saveData.fullscreen ? 1 : 0);

  for (int i = 0; i < HIGH_SCORE_COUNT && len >= 0 && (size_t)len < sizeof(buf); i++)
  {
    // snprintf returns what it *would* have written, so `len` can run past the
    // end of the buffer without a byte of it being touched - and the size
    // handed to the next call is unsigned, where sizeof(buf) - len underflows
    // to something enormous. It cannot happen at these sizes; it is checked
    // because the alternative to checking is finding out.
    int written = snprintf(buf + len, sizeof(buf) - (size_t)len, "hs%d=%d,%d\n",
                           i, saveData.highScores[i], saveData.highLevels[i]);

    if (written < 0)
    {
      len = -1;
      break;
    }

    len += written;
  }

  if (len < 0 || (size_t)len >= sizeof(buf))
  {
    fprintf(stderr, "Save data did not fit its buffer; %s left untouched\n", savePath);
    return;
  }

  SDL_IOStream *file = SDL_IOFromFile(savePath, "w");
  if (file == NULL)
  {
    fprintf(stderr, "Unable to write save file %s\n", savePath);
    return;
  }

  if (SDL_WriteIO(file, buf, (size_t)len) != (size_t)len)
  {
    fprintf(stderr, "Save file %s was written short: %s\n", savePath, SDL_GetError());
  }

  SDL_CloseIO(file);
}

void unlockLevel(int completedLevel)
{
  int newUnlocked = completedLevel + 2; // next level becomes playable
  int total = getNumberOfLevels();

  if (newUnlocked > total)
  {
    newUnlocked = total;
  }

  if (newUnlocked > saveData.unlocked)
  {
    saveData.unlocked = newUnlocked;
    writeSave();
  }
}

int recordScore(int score, int levelReached)
{
  if (score <= 0)
  {
    return -1;
  }

  for (int i = 0; i < HIGH_SCORE_COUNT; i++)
  {
    if (score > saveData.highScores[i])
    {
      // Shift the rest down
      for (int j = HIGH_SCORE_COUNT - 1; j > i; j--)
      {
        saveData.highScores[j] = saveData.highScores[j - 1];
        saveData.highLevels[j] = saveData.highLevels[j - 1];
      }

      saveData.highScores[i] = score;
      saveData.highLevels[i] = levelReached;
      writeSave();
      return i;
    }
  }

  return -1;
}
