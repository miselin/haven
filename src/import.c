#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler.h"
#include "internal.h"
#include "lex.h"
#include "parse.h"

int compiler_parse_import(struct compiler *compiler, enum ImportType type, const char *name) {
  if (type == ImportTypeC) {
    compiler_diag(compiler, DiagError, "C imports not yet supported");
    return -1;
  }

  size_t namelen = strlen(name);
  char *filename = (char *)malloc(namelen + 4);
  strcpy(filename, name);
  strcat(filename, ".hv");

  FILE *in = find_file(compiler, filename);
  if (!in) {
    compiler_diag(compiler, DiagError, "failed to open import file %s\n", filename);
    free(filename);
    return -1;
  }

  struct lex_state *lexer = new_lexer(in, filename, compiler);
  struct parser *parser = new_parser(lexer, compiler);

  int rc = parser_run(parser);

  present_diags(compiler, parser);

  if (rc == 0) {
    rc = parser_merge_asts(compiler->parser, parser);
  }

  destroy_parser(parser);
  destroy_lexer(lexer);
  free(filename);
  return rc;
}
