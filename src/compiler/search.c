#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "compiler.h"
#include "internal.h"

FILE *find_file(struct compiler *compiler, const char *filename) {
  const char *path = NULL;
  if (find_file_path(compiler, filename, &path) < 0) {
    return NULL;
  }

  FILE *result = fopen(path, "r");
  free((void *)path);
  return result;
}

int find_file_path(struct compiler *compiler, const char *filename, const char **discovered_path) {
  if (filename[0] == '/') {
    *discovered_path = strdup(filename);
    return 0;
  }

  struct search_dir *dir = compiler->search_dirs;
  while (dir) {
    char *path = (char *)malloc(strlen(dir->path) + 1 + strlen(filename) + 1);
    strcpy(path, dir->path);
    strcat(path, "/");
    strcat(path, filename);

    struct stat st;
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
      *discovered_path = path;
      return 0;
    }

    free(path);
    dir = dir->next;
  }

  return -1;
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
