#include <llvm-c-18/llvm-c/Types.h>
#include <llvm-c/Core.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "codegen.h"
#include "compiler.h"
#include "internal.h"

static LLVMValueRef emit_preamble_fdecl(struct codegen *codegen, const char *name,
                                        LLVMTypeRef return_type, LLVMTypeRef *out_ftype,
                                        unsigned int num_args, ...);

void emit_preamble(struct codegen *codegen) {
  if (compiler_get_flags(codegen->compiler) & FLAG_NO_PREAMBLE) {
    return;
  }

  LLVMTypeRef voidtype = LLVMVoidTypeInContext(codegen->llvm_context);
  LLVMTypeRef ptrtype = LLVMPointerTypeInContext(codegen->llvm_context, 0);
  LLVMTypeRef i32type = LLVMInt32TypeInContext(codegen->llvm_context);

  // void *__haven_new_empty_box(int box_size);
  codegen->preamble.new_empty_box = emit_preamble_fdecl(
      codegen, "__haven_new_empty_box", ptrtype, &codegen->preamble.new_empty_box_type, 1, i32type);

  // void *__haven_new_box(void *value, int box_size, int type_size);
  codegen->preamble.new_box =
      emit_preamble_fdecl(codegen, "__haven_new_box", ptrtype, &codegen->preamble.new_box_type, 3,
                          ptrtype, i32type, i32type);

  // void __haven_box_ref(void *box):
  codegen->preamble.box_ref = emit_preamble_fdecl(codegen, "__haven_box_ref", voidtype,
                                                  &codegen->preamble.box_ref_type, 1, ptrtype);

  // void __haven_box_unref(void *box):
  codegen->preamble.box_unref = emit_preamble_fdecl(codegen, "__haven_box_unref", voidtype,
                                                    &codegen->preamble.box_unref_type, 1, ptrtype);
}

static LLVMValueRef emit_preamble_fdecl(struct codegen *codegen, const char *name,
                                        LLVMTypeRef return_type, LLVMTypeRef *out_ftype,
                                        unsigned int num_args, ...) {
  LLVMTypeRef *param_types = num_args ? malloc(num_args * sizeof(LLVMTypeRef)) : NULL;
  va_list args;
  va_start(args, num_args);
  for (size_t i = 0; i < num_args; i++) {
    param_types[i] = va_arg(args, LLVMTypeRef);
  }
  va_end(args);

  LLVMTypeRef type = LLVMFunctionType(return_type, param_types, num_args, 0);
  LLVMValueRef result = LLVMAddFunction(codegen->llvm_module, name, type);
  LLVMSetLinkage(result, LLVMExternalLinkage);
  LLVMSetFunctionCallConv(result, LLVMCCallConv);

  if (param_types) {
    free(param_types);
  }

  *out_ftype = type;
  return result;
}
