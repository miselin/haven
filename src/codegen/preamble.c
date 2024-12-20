#include <llvm-c/Core.h>

#include "codegen.h"
#include "internal.h"
#include "utility.h"

void emit_preamble(struct codegen *codegen) {
  UNUSED(codegen);

  // emit global types/declarations that should be in scope for all codegen
}
