/* mattc entry point: compiles MattC code into LLVM IR. */

#include <stdio.h>

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

  int rc = compiler_run(compiler, AllPasses);
  destroy_compiler(compiler);
  return rc;
}
