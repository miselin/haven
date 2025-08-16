#include "ast.h"
#include "compiler.h"

int main(int argc, char *argv[]) {
  struct compiler *compiler = new_compiler(argc, (const char **)argv);
  if (!compiler) {
    return 1;
  }

  int rc = compiler_run(compiler, PassParse);

  dump_ast(compiler_get_ast(compiler));

  destroy_compiler(compiler);
  return rc;
}
