/* mattc entry point: compiles MattC code into LLVM IR. */

#include <stdio.h>

#include "codegen.h"
#include "lex.h"
#include "parse.h"
#include "typecheck.h"
#include "utility.h"

int main(int argc, char *argv[]) {
  UNUSED(argc);
  UNUSED(argv);

  int rc = 0;

  struct lex_state *lexer = new_lexer(stdin);
  struct parser *parser = new_parser(lexer);

  if (parser_run(parser) < 0) {
    fprintf(stderr, "failed!\n");
    rc = 1;
  }

  destroy_lexer(lexer);

  if (rc == 0) {
    struct typecheck *typecheck = new_typecheck(parser_get_ast(parser));
    rc = typecheck_run(typecheck);
    destroy_typecheck(typecheck);
  }

  if (rc == 0) {
    struct codegen *codegen = new_codegen(parser_get_ast(parser));
    rc = codegen_run(codegen);
    if (rc == 0) {
      char *ir = codegen_ir(codegen);
      printf("%s\n", ir);
      codegen_dispose_ir(ir);
    }
    destroy_codegen(codegen);
  }

  destroy_parser(parser);
  return rc;
}
