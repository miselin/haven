#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler.h"
#include "internal.h"
#include "lex.h"
#include "parse.h"

extern int haven_cimport_present(void) __attribute__((weak));

extern int haven_cimport_process(const char *filename) __attribute__((weak));

int compiler_parse_import(struct compiler *compiler, enum ImportType type, const char *name) {
  const char *fullpath = NULL;
  if (find_file_path(compiler, name, &fullpath) < 0) {
    compiler_diag(compiler, DiagError, "failed to find import file %s\n", name);
    return -1;
  }

  if (type == ImportTypeC) {
    if (!haven_cimport_present) {
      compiler_diag(compiler, DiagError, "C imports not supported in this build of the compiler\n");
      free((void *)fullpath);
      return -1;
    }

    fprintf(stderr, "stdin %p stdout %p stderr %p\n", (void *)stdin, (void *)stdout,
            (void *)stderr);

    char cmdbuf[1024];
    snprintf(cmdbuf, sizeof(cmdbuf), "cpp -P %s > /tmp/haven_cimport_tempfile.c", fullpath);

    // run the C preprocessor on the file, output to a temp file for passing to the c importer
    // module
    FILE *fp = popen(cmdbuf, "r");
    if (pclose(fp) < 0) {
      compiler_diag(compiler, DiagError, "failed to run C preprocessor on %s\n", fullpath);
      free((void *)fullpath);
      return -1;
    }

    int rc = haven_cimport_process("/tmp/haven_cimport_tempfile.c");
    if (rc < 0) {
      compiler_diag(compiler, DiagError, "failed to process C import %s\n", fullpath);
    }

    fprintf(stderr, "rc=%d\n", rc);

    free((void *)fullpath);
    return rc;
  }

  FILE *in = fopen(fullpath, "r");
  if (!in) {
    compiler_diag(compiler, DiagError, "failed to open import file %s\n", fullpath);
    free((void *)fullpath);
    return -1;
  }

  struct lex_state *lexer = new_lexer(in, fullpath, compiler);
  struct parser *parser = new_parser(lexer, compiler);

  int rc = parser_run(parser);

  present_diags(compiler, parser);

  if (rc == 0) {
    rc = parser_merge_asts(compiler->parser, parser);
  }

  destroy_parser(parser);
  destroy_lexer(lexer);
  free((void *)fullpath);
  return rc;
}
