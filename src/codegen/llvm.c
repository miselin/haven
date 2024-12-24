#include <llvm-c/Core.h>
#include <llvm-c/Support.h>
#include <llvm-c/TargetMachine.h>
#include <stdio.h>
#include <stdlib.h>

#include "compiler.h"

static int llvm_initialized = 0;

int initialize_llvm(void) {
  if (llvm_initialized) {
    return 0;
  }

  LLVMInitializeAllTargetInfos();
  LLVMInitializeAllTargets();
  LLVMInitializeAllTargetMCs();
  LLVMInitializeAllAsmParsers();
  LLVMInitializeAllAsmPrinters();
  llvm_initialized = 1;

  return 0;
}

void shutdown_llvm(void) {
  if (!llvm_initialized) {
    return;
  }

  llvm_initialized = 0;
  LLVMShutdown();
}

void configure_llvm(struct compiler *compiler) {
  const char **args = malloc(sizeof(char *));
  int nargs = 1;

  args[0] = COMPILER_IDENT;

  size_t flags = compiler_get_flags(compiler);

  // lower matrix intrinsics
  {
    args = realloc(args, sizeof(char *) * (size_t)(nargs + 1));
    args[nargs++] = "-enable-matrix";
  }

  // enable debugging if requested
  if (flags & FLAG_DEBUG_LLVM) {
    args = realloc(args, sizeof(char *) * (size_t)(nargs + 1));
    args[nargs++] = "-debug";

    fprintf(stderr, "enabling llvm debug\n");
  }

  LLVMParseCommandLineOptions(nargs, args, "LLVM internal options");

  free(args);
}
