#include "level-manager.h"
#include "globals.h"
#include "types.h"
#include "embedded_assets.h"
#include <string.h>

#define MAX_LEVELS 100
#define LEVEL_FILE_PREFIX "assets/levels/level"
#define LEVEL_FILE_SUFFIX ".txt"

static Level levels[MAX_LEVELS];
static int numLevels = 0;

// Worked out once, when the levels load, because every frame asks for them.
static int levelWorld[MAX_LEVELS];
static int levelNumberInWorld[MAX_LEVELS];

// What getLevel() hands back for an index it has not got. Filled in by
// initializeLevelManager(), so it is a valid empty level rather than zeroes:
// an all-'_' pattern builds no bricks and no enemies, and the name is what the
// HUD and the level banner print.
static Level fallbackLevel;

const Level *getLevel(int index)
{
  if (index < 0 || index >= numLevels)
  {
    return &fallbackLevel;
  }

  return &levels[index];
}

int getNumberOfLevels(void)
{
  return numLevels;
}

bool isBossLevel(int index)
{
  return getLevel(index)->boss > 0;
}

int worldForLevel(int level)
{
  if (level < 0 || level >= numLevels)
  {
    return 0;
  }

  return levelWorld[level];
}

int levelInWorld(int index)
{
  if (index < 0 || index >= numLevels)
  {
    return 0;
  }

  return levelNumberInWorld[index];
}

// Walks the levels once and hands each one a world. Only the levels that are
// not boss fights advance the count, so the worlds stay exactly the five
// hand-designed levels each that they were drawn as; a boss takes the world of
// the level in front of it, which is the one whose sky it is fought under.
static void assignWorlds(void)
{
  int regular = 0;
  int world = 0;

  for (int i = 0; i < numLevels; i++)
  {
    if (levels[i].boss > 0)
    {
      levelWorld[i] = world;
      levelNumberInWorld[i] = 0;
      continue;
    }

    world = clamp(regular / LEVELS_PER_WORLD, 0, WORLD_COUNT - 1);

    levelWorld[i] = world;
    levelNumberInWorld[i] = regular % LEVELS_PER_WORLD + 1;
    regular++;
  }
}

static bool isValidCell(char c)
{
  return c == 'B' || c == 'D' || c == 'T' || c == 'S' || c == 'X' ||
         c == 'G' || c == 'F' || c == 'E' || c == 'V' || c == 'W' || c == 'U';
}

// Is the text at `i` the `#boss N` directive, rather than a level that happens
// to be called something beginning with those four letters?
//
// The keyword alone used to be the whole test, and `# Boss Rush` passed it: the
// number came out of SDL_atoi(" Rush") as 0, so it was not even read as a boss
// fight, and the line was consumed as a directive instead of taken as the name
// - the level arrived in the HUD called "LEVEL". The level files are a
// documented mod point, so that is a trap laid for somebody else.
//
// The number is what settles it. A directive is the keyword, any separators,
// and then a digit; anything else on that line is a name. `#boss 1`, `#boss:1`
// and `#boss1` are all still the directive, and `# Boss Rush` and
// `# Boss Rush 2` are both still names.
static bool readBossDirective(const char *text, size_t size, size_t i,
                              int *outBoss)
{
  if (i + 4 > size || SDL_strncasecmp(&text[i], "boss", 4) != 0)
  {
    return false;
  }

  size_t j = i + 4;

  while (j < size && (text[j] == ' ' || text[j] == '\t' || text[j] == ':'))
  {
    j++;
  }

  if (j >= size || text[j] < '0' || text[j] > '9')
  {
    return false;
  }

  // The digits are read here rather than by handing the tail of the buffer to
  // SDL_atoi(), and the reason is that `text` is not a C string. A level
  // arrives either as a byte array embed_assets.sh wrote into the binary - no
  // terminator after the last byte, just whatever asset was laid down next -
  // or as a file read into a buffer that is terminated at `size` and no
  // earlier. atoi() looks for a terminator it has not been given, and it got
  // away with that only because every level file in the tree happens to end in
  // a newline. The format is a documented mod point: `#boss 7` as the last
  // line of a file saved without one is a read off the end of the array.
  //
  // The accumulator stops growing at four digits rather than wrapping, because
  // a nonsense number should stay an obviously wrong one - signed overflow is
  // undefined, and the value is only ever compared against the seven entries
  // in bossDefs[].
  int boss = 0;

  while (j < size && text[j] >= '0' && text[j] <= '9')
  {
    if (boss < 1000)
    {
      boss = boss * 10 + (text[j] - '0');
    }

    j++;
  }

  *outBoss = boss;

  return true;
}

// Load one level from a text buffer (embedded or file data).
// A line starting with '#' sets the level name; other cells are pattern chars.
static bool parseLevelFromText(Level *level, const char *text, size_t size)
{
  memset(level->pattern, '_', sizeof(level->pattern));
  level->name[0] = '\0';
  level->boss = 0;

  int row = 0;
  size_t i = 0;

  while (i < size && row < LEVEL_PATTERN_ROWS)
  {
    // Name / comment line
    if (text[i] == '#')
    {
      i++;
      while (i < size && (text[i] == ' ' || text[i] == '\t'))
      {
        i++;
      }

      // `#boss N` is a directive rather than a comment, and it is checked
      // before the name is taken: a boss level names itself on its first `#`
      // line like any other, and this must not be able to become that name.
      int bossNumber = 0;

      if (readBossDirective(text, size, i, &bossNumber))
      {
        level->boss = bossNumber;

        while (i < size && text[i] != '\n' && text[i] != '\r')
        {
          i++;
        }

        // Skip newline(s) and carry on with the next line; this one was not a
        // pattern row and must not advance `row`.
        while (i < size && (text[i] == '\n' || text[i] == '\r'))
        {
          i++;
        }

        continue;
      }

      bool firstNameLine = level->name[0] == '\0';
      int len = 0;
      while (i < size && text[i] != '\n' && text[i] != '\r')
      {
        if (firstNameLine && len < LEVEL_NAME_MAX - 1)
        {
          level->name[len++] = text[i];
        }
        i++;
      }
      if (firstNameLine)
      {
        level->name[len] = '\0';
      }
    }
    else
    {
      // Pattern line
      int col = 0;
      while (i < size && text[i] != '\n' && text[i] != '\r')
      {
        if (col < LEVEL_PATTERN_COLS)
        {
          char c = text[i];
          level->pattern[row * LEVEL_PATTERN_COLS + col] = isValidCell(c) ? c : '_';
          col++;
        }
        i++;
      }
      row++;
    }

    // Skip newline(s)
    while (i < size && (text[i] == '\n' || text[i] == '\r'))
    {
      i++;
    }
  }

  if (level->name[0] == '\0')
  {
    snprintf(level->name, LEVEL_NAME_MAX, "LEVEL");
  }

  return true;
}

// Load a single level txt file by 1-based index
static bool loadLevelFile(int index)
{
  char filename[256];
  snprintf(filename, sizeof(filename), "%s%02d%s", LEVEL_FILE_PREFIX, index, LEVEL_FILE_SUFFIX);

  const EmbeddedAsset *asset = findEmbeddedAsset(filename);

  if (asset != NULL)
  {
    return parseLevelFromText(&levels[numLevels], (const char *)asset->data, asset->size);
  }

  // Try filesystem fallback
  SDL_IOStream *file = SDL_IOFromFile(filename, "r");
  if (file == NULL)
  {
    return false;
  }

  Sint64 fileSize = SDL_GetIOSize(file);
  if (fileSize <= 0)
  {
    SDL_CloseIO(file);
    return false;
  }

  char *buf = (char *)SDL_malloc((size_t)fileSize + 1);
  if (buf == NULL)
  {
    SDL_CloseIO(file);
    return false;
  }

  size_t bytesRead = SDL_ReadIO(file, buf, (size_t)fileSize);
  SDL_CloseIO(file);
  buf[bytesRead] = '\0';

  bool ok = parseLevelFromText(&levels[numLevels], buf, bytesRead);
  SDL_free(buf);
  return ok;
}

int getLevelContentBottom(int levelIndex)
{
  if (levelIndex < 0 || levelIndex >= numLevels)
  {
    return LEVEL_PATTERN_TOP + LEVEL_PATTERN_ROWS * (LEVEL_PATTERN_ROW_HEIGHT + LEVEL_PATTERN_SPACING);
  }

  const Level *level = &levels[levelIndex];

  int lastRow = -1;
  for (int row = LEVEL_PATTERN_ROWS - 1; row >= 0; row--)
  {
    for (int col = 0; col < LEVEL_PATTERN_COLS; col++)
    {
      if (level->pattern[row * LEVEL_PATTERN_COLS + col] != '_')
      {
        lastRow = row;
        break;
      }
    }
    if (lastRow >= 0)
    {
      break;
    }
  }

  if (lastRow < 0)
  {
    return SCREEN_HEIGHT / 2;
  }

  return LEVEL_PATTERN_TOP + (lastRow + 1) * (LEVEL_PATTERN_ROW_HEIGHT + LEVEL_PATTERN_SPACING);
}

void initializeLevelManager(void)
{
  numLevels = 0;

  memset(fallbackLevel.pattern, '_', sizeof(fallbackLevel.pattern));
  snprintf(fallbackLevel.name, LEVEL_NAME_MAX, "LEVEL");
  fallbackLevel.boss = 0;

  for (int i = 1; i <= MAX_LEVELS; i++)
  {
    if (!loadLevelFile(i))
    {
      break;
    }
    numLevels++;
  }

  assignWorlds();

  if (numLevels == 0)
  {
    fprintf(stderr, "Warning: no level files found.\n");
  }
  else
  {
    printf("Loaded %d levels.\n", numLevels);
  }
}

void destroyLevelManager(void)
{
  numLevels = 0;
}
