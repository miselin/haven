#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler.h"
#include "internal.h"
#include "lex.h"
#include "parse.h"

extern int haven_cimport_present(void) __attribute__((weak));

extern int haven_cimport_process(const char *filename) __attribute__((weak));

int cimport(struct parser *parser, const char *filename);

int compiler_parse_import(struct compiler *compiler, enum ImportType type, const char *name) {
  const char *fullpath = NULL;
  if (find_file_path(compiler, name, &fullpath) < 0) {
    compiler_diag(compiler, DiagError, "failed to find import file %s\n", name);
    return -1;
  }

  if (type == ImportTypeC) {
    if (cimport(compiler->parser, fullpath) < 0) {
      compiler_diag(compiler, DiagError, "failed to process C import %s\n", fullpath);
      free((void *)fullpath);
      return -1;
    }

    free((void *)fullpath);
    return 0;
  }

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
    rc = parser_merge_asts(compiler->parser, parser);
  }

  destroy_parser(parser);
  destroy_lexer(lexer);
  free((void *)fullpath);
  return rc;
}
