/* mattc entry point: compiles MattC code into LLVM IR. */

#include <stdio.h>

#include "ast.h"
#include "cfold.h"
#include "codegen.h"
#include "compiler.h"
#include "lex.h"
#include "parse.h"
#include "purity.h"
#include "typecheck.h"
#include "utility.h"

int main(int argc, char *argv[]) {
  struct compiler *compiler = new_compiler(argc, (const char **)argv);
  if (!compiler) {
    return 1;
  }

  int rc = compiler_run(compiler, PassParse);
  if (rc == 0) {
    struct ast_program *ast = compiler_get_ast(compiler);
    emit_ast_as_code(ast, stdout);
  }

  destroy_compiler(compiler);
  return rc;
}
