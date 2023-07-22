#include "save.h"
#include "../version.h"
#include "../level-manager.h"
#include <errno.h>
#include <limits.h>
#include <string.h>

SaveData saveData;

static char savePath[512];

// See setSaveScripted() in save.h for why this is here.
static bool scripted;

// See setSaveReadOnly() in save.h.
static bool readOnly;

// Set when there is nowhere sane to put the file: SDL_GetPrefPath() failed or
// its answer did not fit savePath. Both loadSave() and writeSave() then do
// nothing, which beats the old fallback of a "save.txt" in whatever directory
// the game happened to be started from.
static bool saveDisabled;

void setSaveScripted(bool value)
{
  scripted = value;
}

void setSaveReadOnly(bool value)
{
  readOnly = value;
}

static void resolveSavePath(void)
{
  if (savePath[0] != '\0' || saveDisabled)
  {
    return;
  }

  char *pref = SDL_GetPrefPath(BREAKUP_SAVE_ORG, BREAKUP_SAVE_APP);

  if (pref == NULL)
  {
    fprintf(stderr, "No preference directory (%s); saving disabled\n", SDL_GetError());
    saveDisabled = true;
    return;
  }

  int len = snprintf(savePath, sizeof(savePath), "%ssave.txt", pref);

  if (len < 0 || (size_t)len >= sizeof(savePath))
  {
    fprintf(stderr, "Save path %s is longer than %zu bytes; saving disabled\n",
            pref, sizeof(savePath) - 1);
    savePath[0] = '\0';
    saveDisabled = true;
  }

  SDL_free(pref);
}

static void resetSaveData(void)
{
  saveData.unlocked = 1;
  saveData.musicVol = 0.7f;
  saveData.sfxVol = 0.8f;
  saveData.fullscreen = false;

  for (int i = 0; i < HIGH_SCORE_COUNT; i++)
  {
    saveData.highScores[i] = 0;
    saveData.highLevels[i] = 0;
  }
}

// Trailing whitespace is allowed after a number and nothing else is, so a file
// saved with CRLF line endings still reads and `music=0.5abc` does not.
static bool restIsBlank(const char *s)
{
  while (*s == ' ' || *s == '\t' || *s == '\r')
  {
    s++;
  }

  return *s == '\0';
}

// The whole of `s` as an int, or false. This used to be sscanf("%d"), which is
// undefined behaviour on a number that does not fit an int - and this file is
// plain text in the player's own directory. strtol saturates instead and says
// so through ERANGE; the value is then brought into int range and the callers'
// own clamps take it from there.
//
// Nothing in the tree calls setlocale(), so this and strtod() below run in the
// "C" locale, which is the one writeSave()'s "%d" and "%.2f" write in.
static bool parseInt(const char *s, int *out)
{
  char *end;

  errno = 0;
  long value = strtol(s, &end, 10);

  if (end == s || !restIsBlank(end))
  {
    return false;
  }

  if (errno == ERANGE || value > INT_MAX)
  {
    value = value < 0 ? INT_MIN : INT_MAX;
  }
  else if (value < INT_MIN)
  {
    value = INT_MIN;
  }

  *out = (int)value;

  return true;
}

// The whole of `s` as a float, or false. Overflow comes out as an infinity and
// "nan" as a NaN, and both are left to the caller's clamp - see clampFloat() in
// globals.h for why that clamp is written the way it is.
static bool parseFloat(const char *s, float *out)
{
  char *end;
  double value = strtod(s, &end);

  if (end == s || !restIsBlank(end))
  {
    return false;
  }

  *out = (float)value;

  return true;
}

// One `key=value` line. `line` is NUL-terminated and may be edited in place.
static void parseSaveLine(char *line)
{
  char *eq = strchr(line, '=');

  if (eq == NULL)
  {
    return;
  }

  *eq = '\0';

  const char *key = line;
  const char *value = eq + 1;
  int iv;
  float fv;

  if (strcmp(key, "unlocked") == 0)
  {
    if (parseInt(value, &iv))
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
  }
  else if (strcmp(key, "music") == 0)
  {
    if (parseFloat(value, &fv))
    {
      saveData.musicVol = clamp(fv, 0.0f, 1.0f);
    }
  }
  else if (strcmp(key, "sfx") == 0)
  {
    if (parseFloat(value, &fv))
    {
      saveData.sfxVol = clamp(fv, 0.0f, 1.0f);
    }
  }
  else if (strcmp(key, "fullscreen") == 0)
  {
    if (parseInt(value, &iv))
    {
      saveData.fullscreen = iv != 0;
    }
  }
  else if (key[0] == 'h' && key[1] == 's')
  {
    // hsN=score,level
    int slot;
    char *comma = strchr(eq + 1, ',');

    if (comma == NULL || !parseInt(key + 2, &slot) || slot < 0 ||
        slot >= HIGH_SCORE_COUNT)
    {
      return;
    }

    *comma = '\0';

    int score, level;

    if (parseInt(value, &score) && parseInt(comma + 1, &level))
    {
      // Clamped for the same reason `unlocked` above is: this file is plain
      // text in the player's own directory and nothing stops it being
      // edited, truncated or written by an older build. recordScore() only
      // ever stores a positive score and a level that exists, so anything
      // else arriving here is not a score - and the one thing that must not
      // happen is the high score table printing "-00005" at somebody.
      int total = getNumberOfLevels();

      saveData.highScores[slot] = score > 0 ? score : 0;
      saveData.highLevels[slot] = total > 0 ? clamp(level, 0, total) : 0;
    }
  }
}

void loadSaveFromText(const char *text, size_t size)
{
  resetSaveData();

  if (text == NULL)
  {
    return;
  }

  size_t i = 0;

  while (i < size)
  {
    // One line, copied out so it can be NUL-terminated and cut at the '=' and
    // the ','. No line this file writes is anywhere near this long; one that
    // is cannot be a key it knows and is skipped whole.
    char line[128];
    size_t len = 0;

    while (i < size && text[i] != '\n' && text[i] != '\0')
    {
      if (len < sizeof(line) - 1)
      {
        line[len] = text[i];
      }

      len++;
      i++;
    }

    // Past the '\n' (or the NUL, which is treated as one)
    if (i < size)
    {
      i++;
    }

    if (len < sizeof(line))
    {
      line[len] = '\0';
      parseSaveLine(line);
    }
  }
}

void loadSave(void)
{
  resetSaveData();

  if (scripted)
  {
    return;
  }

  resolveSavePath();

  if (saveDisabled)
  {
    return;
  }

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

  char buf[8192];
  size_t read = SDL_ReadIO(file, buf, (size_t)size);
  SDL_CloseIO(file);

  loadSaveFromText(buf, read);
}

void writeSave(void)
{
  if (scripted || readOnly)
  {
    return;
  }

  resolveSavePath();

  if (saveDisabled)
  {
    return;
  }

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
