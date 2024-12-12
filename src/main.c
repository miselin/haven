/* mattc entry point: compiles MattC code into LLVM IR. */

#include <stdio.h>

#include "cfold.h"
#include "codegen.h"
#include "lex.h"
#include "parse.h"
#include "typecheck.h"
#include "utility.h"

int main(int argc, char *argv[]) {
  const char *filename = "<stdin>";

  FILE *in = stdin;
  if (argc > 1) {
    in = fopen(argv[1], "r");
    if (!in) {
      perror("fopen");
      return 1;
    }

    filename = argv[1];
  }

  FILE *out = stdout;
  if (argc > 2) {
    out = fopen(argv[2], "w");
    if (!out) {
      perror("fopen");
      return 1;
    }
  }

  int rc = 0;

  struct lex_state *lexer = new_lexer(in, filename);
  struct parser *parser = new_parser(lexer);

  if (parser_run(parser) < 0) {
    rc = 1;
  }

  if (rc == 0) {
    struct cfolder *cfolder = new_cfolder(parser_get_ast(parser));
    rc = cfolder_run(cfolder);
    destroy_cfolder(cfolder);
  }

  if (rc == 0) {
    struct typecheck *typecheck = new_typecheck(parser_get_ast(parser));
    rc = typecheck_run(typecheck);
    destroy_typecheck(typecheck);
  }

  if (rc == 0) {
    fprintf(stderr, "== Pre-codegen AST ==\n");
    dump_ast(parser_get_ast(parser));

    struct codegen *codegen = new_codegen(parser_get_ast(parser));
    rc = codegen_run(codegen);
    if (rc == 0) {
      char *ir = codegen_ir(codegen);
      fprintf(out, "%s\n", ir);
      codegen_dispose_ir(ir);

      fflush(out);
      if (out != stdout) {
        fclose(out);
      }
    }
    destroy_codegen(codegen);
  }

  if (rc) {
    fprintf(stderr, "== Partial AST after failure ==\n");
    dump_ast(parser_get_ast(parser));
  }

  destroy_parser(parser);
  destroy_lexer(lexer);
  return rc;
}
