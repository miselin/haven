#include <llvm-c-18/llvm-c/Types.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <malloc.h>

#include "ast.h"
#include "codegen.h"
#include "internal.h"
#include "kv.h"
#include "scope.h"
#include "utility.h"

void emit_fdecl(struct codegen *codegen, struct ast_fdecl *fdecl) {
  LLVMValueRef func = NULL;
  LLVMTypeRef *param_types = NULL;

  struct scope_entry *entry = scope_lookup(codegen->scope, fdecl->ident.value.identv.ident, 0);
  if (!entry) {
    // emit declaration
    param_types = malloc(sizeof(LLVMTypeRef) * fdecl->num_params);
    for (size_t i = 0; i < fdecl->num_params; i++) {
      param_types[i] = ast_ty_to_llvm_ty(&fdecl->params[i]->ty);
    }
    LLVMTypeRef ret_type =
        LLVMFunctionType(ast_ty_to_llvm_ty(&fdecl->retty), param_types,
                         (unsigned int)fdecl->num_params, fdecl->flags & DECL_FLAG_VARARG);
    func = LLVMAddFunction(codegen->llvm_module, fdecl->ident.value.identv.ident, ret_type);
    if (fdecl->flags & DECL_FLAG_PUB) {
      LLVMSetLinkage(func, LLVMExternalLinkage);
    } else {
      LLVMSetLinkage(func, LLVMInternalLinkage);
    }

    entry = calloc(1, sizeof(struct scope_entry));
    entry->fdecl = fdecl;
    entry->function_type = ret_type;
    entry->param_types = param_types;
    entry->ref = func;
    scope_insert(codegen->scope, fdecl->ident.value.identv.ident, entry);
  } else {
    func = entry->ref;
    param_types = entry->param_types;
  }

  // generate definition if we have one
  if (fdecl->body) {
    codegen->entry_block = LLVMAppendBasicBlock(func, "entry");
    codegen->last_alloca = NULL;
    LLVMPositionBuilderAtEnd(codegen->llvm_builder, codegen->entry_block);

    codegen->current_function = func;
    codegen->locals = new_kv();

    codegen->scope = enter_scope(codegen->scope);

    for (size_t i = 0; i < fdecl->num_params; i++) {
      struct scope_entry *param_entry = calloc(1, sizeof(struct scope_entry));
      param_entry->vdecl = fdecl->params[i];
      param_entry->variable_type = param_types[i];
      param_entry->ref = LLVMGetParam(func, (unsigned int)i);
      scope_insert(codegen->scope, fdecl->params[i]->ident.value.identv.ident, param_entry);
    }

    LLVMValueRef block_result = emit_block(codegen, fdecl->body);
    if (block_result) {
      LLVMBuildRet(codegen->llvm_builder, block_result);
    } else {
      if (fdecl->retty.ty == AST_TYPE_VOID) {
        LLVMBuildRetVoid(codegen->llvm_builder);
      } else {
        // semantic analysis should catch this case, but handle it just in case
        // TODO: need to generate a return value based on the function's return type
        LLVMBuildRet(codegen->llvm_builder, LLVMConstInt(LLVMInt32Type(), 0, 0));
      }
    }

    codegen->scope = exit_scope(codegen->scope);

    destroy_kv(codegen->locals);
    codegen->current_function = NULL;
  }
}
