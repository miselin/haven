#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "cimport.h"
#include "compiler.h"
#include "internal.h"
#include "lex.h"
#include "parse.h"

extern int haven_cimport_present(void) __attribute__((weak));

extern int haven_cimport_process(const char *filename) __attribute__((weak));

int compiler_parse_import(struct compiler *compiler, enum ImportType type, const char *name,
                          struct ast_import *into) {
  if (type == ImportTypeC && ((compiler->flags[0] & FLAG_BOOTSTRAP) == 0)) {
    compiler_diag(
        compiler, DiagError,
        "C imports are only available in bootstrap mode as they have very poor ergonomics\n");
  }

  const char *fullpath = NULL;
  if (find_file_path(compiler, name, &fullpath) < 0) {
    compiler_diag(compiler, DiagError, "failed to find import file %s\n", name);
    return -1;
  }

  struct stat st;
  stat(fullpath, &st);

  struct imported_file *cursor = compiler->imported_files;
  while (cursor) {
    if (same_file(&cursor->st, &st)) {
      // already imported, generate an empty AST for the import node
      into->ast = calloc(1, sizeof(struct ast_program));
      strncpy(into->ast->loc.file, fullpath, 256);
      free((void *)fullpath);
      return 0;
    }
    cursor = cursor->next;
  }

  if (type == ImportTypeC) {
    if (!S_ISREG(st.st_mode)) {
      compiler_diag(compiler, DiagError, "not a regular file: %s\n", fullpath);
      free((void *)fullpath);
      return -1;
    }

    if (cimport(compiler->cimporter, fullpath) < 0) {
      compiler_diag(compiler, DiagError, "failed to process C import %s\n", fullpath);
      free((void *)fullpath);
      return -1;
    }

    track_imported_file(compiler, &st);

    free((void *)fullpath);
    return 0;
  }

  if (S_ISDIR(st.st_mode)) {
    // need there to be an index.hv when importing a directory
    fullpath = realloc((void *)fullpath, strlen(fullpath) + 1 + 9 + 1);
    strcat((char *)fullpath, "/index.hv");

    if (stat(fullpath, &st) < 0) {
      compiler_diag(compiler, DiagError, "failed to find index.hv in import directory %s\n", name);
      free((void *)fullpath);
      return -1;
    }

    if (!S_ISREG(st.st_mode)) {
      compiler_diag(compiler, DiagError, "index.hv is not a regular file in import directory %s\n",
                    name);
      free((void *)fullpath);
      return -1;
    }
  }

  track_imported_file(compiler, &st);

  FILE *in = fopen(fullpath, "r");
  if (!in) {
    compiler_diag(compiler, DiagError, "failed to open import file %s\n", fullpath);
    free((void *)fullpath);
    return -1;
  }

  struct lex_state *lexer = new_lexer(in, fullpath, compiler);
  struct parser *parser = new_parser(lexer, compiler);

  int rc = parser_run(parser, 0);

  present_diags(compiler, parser);

  if (rc == 0) {
    rc = parser_merge_into(parser, into);
  }

  destroy_parser(parser);
  destroy_lexer(lexer);
  free((void *)fullpath);
  return rc;
}

int same_file(struct stat *a, struct stat *b) {
  return a->st_dev == b->st_dev && a->st_ino == b->st_ino;
}

void track_imported_file(struct compiler *compiler, struct stat *st) {
  struct imported_file *this_entry = calloc(1, sizeof(struct imported_file));
  this_entry->st = *st;
  this_entry->next = compiler->imported_files;
  compiler->imported_files = this_entry;
}

int compiler_finalize_imports(struct compiler *compiler, struct ast_import *into) {
  return cimport_finalize(compiler->cimporter, into);
}
