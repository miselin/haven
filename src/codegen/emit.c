#include <llvm-c-18/llvm-c/TargetMachine.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/Core.h>
#include <stdio.h>

#include "codegen.h"
#include "internal.h"

static int emit_membuf(LLVMMemoryBufferRef membuf, FILE *stream) {
  const char *buf = LLVMGetBufferStart(membuf);
  size_t len = LLVMGetBufferSize(membuf);
  if (fwrite(buf, 1, len, stream) != len) {
    fprintf(stderr, "error or truncated write to output file\n");
    return 1;
  }
  fflush(stream);
  LLVMDisposeMemoryBuffer(membuf);
  return 0;
}

int codegen_emit_ir(struct codegen *codegen, FILE *stream) {
  char *ir = LLVMPrintModuleToString(codegen->llvm_module);
  if (!ir) {
    return 1;
  }

  fputs(ir, stream);
  LLVMDisposeMessage(ir);
  return 0;
}

int codegen_emit_bitcode(struct codegen *codegen, FILE *stream) {
  LLVMMemoryBufferRef membuf = LLVMWriteBitcodeToMemoryBuffer(codegen->llvm_module);

  if (!membuf) {
    return 1;
  }

  return emit_membuf(membuf, stream);
}

int codegen_emit_obj(struct codegen *codegen, FILE *stream) {
  char *error = NULL;
  LLVMMemoryBufferRef membuf = NULL;
  LLVMTargetMachineEmitToMemoryBuffer(codegen->llvm_target_machine, codegen->llvm_module,
                                      LLVMObjectFile, &error, &membuf);
  if (error) {
    fprintf(stderr, "error emitting object: %s\n", error);
  }
  LLVMDisposeMessage(error);

  if (!membuf) {
    return 1;
  }

  return emit_membuf(membuf, stream);
}

int codegen_emit_asm(struct codegen *codegen, FILE *stream) {
  char *error = NULL;
  LLVMMemoryBufferRef membuf = NULL;
  LLVMTargetMachineEmitToMemoryBuffer(codegen->llvm_target_machine, codegen->llvm_module,
                                      LLVMAssemblyFile, &error, &membuf);
  if (error) {
    fprintf(stderr, "error emitting asm: %s\n", error);
  }
  LLVMDisposeMessage(error);

  if (!membuf) {
    return 1;
  }

  return emit_membuf(membuf, stream);
}
