#pragma once

#include "../globals.h"

bool isTransitionActive(void);

void startTransition(void (*callback)(void));

void initializeTransition(void);

void updateTransition(void);

void drawTransition(void);

void destroyTransition(void);
