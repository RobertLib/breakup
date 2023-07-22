#include "list.h"

// Create a new list with initialization to NULL using calloc
List *newList(void)
{
  List *list = (List *)calloc(1, sizeof(List));

  if (list == NULL)
  {
    fprintf(stderr, "Failed to allocate memory for the list.\n");
    return NULL;
  }

  return list;
}

// Add a new item to the list
void listAdd(List *list, void *data)
{
  if (list == NULL)
  {
    fprintf(stderr, "Provided list is NULL.\n");
    return;
  }

  Node *newNode = (Node *)malloc(sizeof(Node));

  if (newNode == NULL)
  {
    fprintf(stderr, "Failed to allocate memory for newNode.\n");
    return;
  }

  newNode->data = data;
  newNode->next = NULL;

  if (list->tail == NULL)
  {
    list->head = newNode;
    list->tail = newNode;
  }
  else
  {
    list->tail->next = newNode;
    list->tail = newNode;
  }
}

// Free a single node.
//
// The node only, never `node->data`: a list here holds pointers into an array
// somebody else owns - the brick grid in src/bricks/bricks.c points every cell
// at elements of the one `bricks` allocation - so freeing the payload would be
// freeing the middle of that array. The caller owns what it put in.
static void freeNode(Node *node)
{
  free(node);
}

// Free the entire list
void freeList(List *list)
{
  if (list == NULL)
  {
    return;
  }

  Node *current = list->head;

  while (current != NULL)
  {
    Node *next = current->next;
    freeNode(current);
    current = next;
  }

  free(list);
}
