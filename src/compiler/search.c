#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler.h"
#include "internal.h"

FILE *find_file(struct compiler *compiler, const char *filename) {
  if (filename[0] == '/') {
    return fopen(filename, "r");
  }

  struct search_dir *dir = compiler->search_dirs;
  while (dir) {
    char *path = (char *)malloc(strlen(dir->path) + 1 + strlen(filename) + 1);
    strcpy(path, dir->path);
    strcat(path, "/");
    strcat(path, filename);
    FILE *result = fopen(path, "r");
    free(path);
    if (result) {
      return result;
    }
    dir = dir->next;
  }

  return NULL;
}

void add_search_dir(struct compiler *compiler, const char *path) {
  // check for duplicates
  struct search_dir *dir = compiler->search_dirs;
  struct search_dir *tail = NULL;
  while (dir) {
    if (strcmp(dir->path, path) == 0) {
      return;
    }
    tail = dir;
    dir = dir->next;
  }

  struct search_dir *new_dir = (struct search_dir *)malloc(sizeof(struct search_dir));
  new_dir->path = strdup(path);
  new_dir->next = NULL;

  if (compiler->flags[0] & FLAG_VERBOSE) {
    fprintf(stderr, "adding search dir: %s\n", path);
  }

  if (tail) {
    tail->next = new_dir;
  } else {
    compiler->search_dirs = new_dir;
  }
}
