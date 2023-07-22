#pragma once

#include "../globals.h"

typedef struct Node
{
  void *data;
  struct Node *next;
} Node;

typedef struct List
{
  Node *head;
  Node *tail;
} List;

List *newList(void);

void listAdd(List *list, void *data);

void freeList(List *list);
