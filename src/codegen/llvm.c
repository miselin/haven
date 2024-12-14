#include <llvm-c/Core.h>
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

  LLVMShutdown();

  return 0;
}

void shutdown_llvm() {
  if (!llvm_initialized) {
    return;
  }

  llvm_initialized = 0;
  LLVMShutdown();
}
