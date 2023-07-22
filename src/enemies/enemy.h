#pragma once

#include "../globals.h"
#include "../types.h"

typedef struct Enemy
{
  Vec2 pos;
  Vec2 vel; // normalized direction
  float speed;
  bool active;
  EnemyType type;
  float animTime;
  float actionTimer;  // direction change / dive delay / shoot cooldown
  float spawnFade;    // 0..1 fade-in after spawning
  int spawnerIndex;   // owning spawn point, -1 for splitter minis
} Enemy;

// Pixel size of an enemy of the given type
int enemySize(EnemyType type);

// Whether this enemy can be struck yet: active, and on screen at all.
// drawEnemy() draws it at `spawnFade * 255` alpha, so one that has not been
// updated once is drawn at nothing - and what cannot be seen does not stop a
// ball, a bolt or a blast. It is the same rule queryBricksNear() applies with
// `spawnT > 0` to a brick that has not popped in yet.
//
// It is also the whole of the splitter fix, and the reason it is a function
// rather than four copies of `enemy->active`. killEnemy() puts a splitter's
// two minis into the same enemies[] array its caller is still walking, and
// findInactiveEnemy() hands back the lowest free slot - so the second mini
// usually lands *above* the splitter's index, and the caller's own loop reaches
// it later in the same pass, while whatever killed the splitter is still
// sitting on the spot the mini was born at. One mini in eleven died on the
// frame it was created, paying the player 300 points for a splitter that only
// ever produced one half.
//
// Four callers, and the fourth is the awkward one. The ball, the laser and an
// explosion all ask from outside updateEnemy() and get `false` for a mini that
// has not been updated yet. The paddle test lives *inside* updateEnemy(),
// after the line that advances spawnFade - so it has to take its answer before
// that line rather than call this one afterwards. Missing it left exactly one
// route open: a splitter driven into by the paddle drops a mini onto the
// paddle, and updateEnemies() reaches that mini's slot later in the same pass.
bool enemyIsTargetable(const Enemy *enemy);

void initializeEnemy(Enemy *enemy, EnemyType type, float x, float y, int spawnerIndex);

void updateEnemy(Enemy *enemy);

void drawEnemy(const Enemy *enemy);
