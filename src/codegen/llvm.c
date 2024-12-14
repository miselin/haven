#include <llvm-c/TargetMachine.h>

static int llvm_initialized = 0;

int initialize_llvm() {
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
