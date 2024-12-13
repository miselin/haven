#include <llvm-c-18/llvm-c/DebugInfo.h>
#include <llvm-c-18/llvm-c/Types.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <malloc.h>
#include <string.h>

#include "ast.h"
#include "codegen.h"
#include "internal.h"
#include "kv.h"
#include "lex.h"
#include "scope.h"
#include "utility.h"

void emit_fdecl(struct codegen *codegen, struct ast_fdecl *fdecl, struct lex_locator *at) {
  LLVMValueRef func = NULL;
  LLVMTypeRef ret_ty = ast_ty_to_llvm_ty(codegen, &fdecl->retty);
  LLVMTypeRef *param_types = NULL;

  struct scope_entry *entry = scope_lookup(codegen->scope, fdecl->ident.value.identv.ident, 0);
  if (!entry) {
    // emit declaration
    param_types = malloc(sizeof(LLVMTypeRef) * fdecl->num_params);
    for (size_t i = 0; i < fdecl->num_params; i++) {
      param_types[i] = ast_ty_to_llvm_ty(codegen, &fdecl->params[i]->ty);
    }
    LLVMTypeRef func_type = LLVMFunctionType(ret_ty, param_types, (unsigned int)fdecl->num_params,
                                             fdecl->flags & DECL_FLAG_VARARG);
    func = LLVMAddFunction(codegen->llvm_module, fdecl->ident.value.identv.ident, func_type);
    if (fdecl->flags & DECL_FLAG_PUB) {
      LLVMSetLinkage(func, LLVMExternalLinkage);
    } else {
      LLVMSetLinkage(func, LLVMInternalLinkage);
    }

    {
      unsigned int sanitize = LLVMGetEnumAttributeKindForName("sanitize_address", 16);
      unsigned int memory = LLVMGetEnumAttributeKindForName("memory", 6);

      // sanitize_address for ASAN - need cli flags for this
      LLVMContextRef context = codegen->llvm_context;
      LLVMAttributeRef ref = LLVMCreateEnumAttribute(context, sanitize, 0);
      LLVMAddAttributeAtIndex(func, (LLVMAttributeIndex)LLVMAttributeFunctionIndex, ref);

      if ((fdecl->flags & DECL_FLAG_IMPURE) == 0) {
        ref = LLVMCreateEnumAttribute(context, memory, 0);
        LLVMAddAttributeAtIndex(func, (LLVMAttributeIndex)LLVMAttributeFunctionIndex, ref);
      }
    }

    entry = calloc(1, sizeof(struct scope_entry));
    entry->fdecl = fdecl;
    entry->function_type = func_type;
    entry->param_types = param_types;
    entry->ref = func;
    scope_insert(codegen->scope, fdecl->ident.value.identv.ident, entry);
  } else {
    func = entry->ref;
    param_types = entry->param_types;
  }

  // generate definition if we have one
  if (!fdecl->body) {
    return;
  }

  LLVMMetadataRef function_metadata = LLVMDIBuilderCreateFunction(
      codegen->llvm_dibuilder, codegen->compile_unit, fdecl->ident.value.identv.ident,
      strlen(fdecl->ident.value.identv.ident), "", 0, codegen->file_metadata,
      (unsigned)at->line + 1, NULL, (fdecl->flags & DECL_FLAG_PUB) == 0, fdecl->body != NULL, 1, 0,
      0);

  codegen->current_function_metadata = function_metadata;
  update_debug_loc(codegen, at);

  LLVMContextRef context = codegen->llvm_context;
  codegen->return_block = LLVMCreateBasicBlockInContext(context, "return");

  codegen->entry_block = LLVMAppendBasicBlockInContext(context, func, "entry");
  codegen->last_alloca = NULL;
  LLVMPositionBuilderAtEnd(codegen->llvm_builder, codegen->entry_block);

  if (fdecl->retty.ty != AST_TYPE_VOID) {
    codegen->retval = new_alloca(codegen, ret_ty, "retval");
  }

  codegen->current_function = func;
  codegen->locals = new_kv();

  codegen->defer_head = NULL;

  codegen_internal_enter_scope(codegen, at, 0);

  for (size_t i = 0; i < fdecl->num_params; i++) {
    struct scope_entry *param_entry = calloc(1, sizeof(struct scope_entry));
    param_entry->vdecl = fdecl->params[i];
    param_entry->variable_type = param_types[i];
    param_entry->ref = LLVMGetParam(func, (unsigned int)i);
    scope_insert(codegen->scope, fdecl->params[i]->ident.value.identv.ident, param_entry);
  }

  LLVMValueRef block_result = emit_block(codegen, fdecl->body);
  if (fdecl->retty.ty != AST_TYPE_VOID) {
    emit_store(codegen, &fdecl->retty, block_result, codegen->retval);
  }

  LLVMBasicBlockRef defers = LLVMCreateBasicBlockInContext(context, "defers");
  LLVMBuildBr(codegen->llvm_builder, defers);

  LLVMAppendExistingBasicBlock(codegen->current_function, defers);
  LLVMPositionBuilderAtEnd(codegen->llvm_builder, defers);

  // run defer expressions, if any
  struct defer_entry *defer = codegen->defer_head;
  while (defer) {
    struct defer_entry *next = defer->next;
    emit_expr(codegen, defer->expr);
    free(defer);
    defer = next;
  }

  LLVMBuildBr(codegen->llvm_builder, codegen->return_block);

  // insert return block finally
  LLVMAppendExistingBasicBlock(codegen->current_function, codegen->return_block);
  LLVMPositionBuilderAtEnd(codegen->llvm_builder, codegen->return_block);

  if (fdecl->retty.ty == AST_TYPE_VOID) {
    LLVMBuildRetVoid(codegen->llvm_builder);
  } else {
    LLVMValueRef retval = LLVMBuildLoad2(codegen->llvm_builder, ret_ty, codegen->retval, "");
    LLVMBuildRet(codegen->llvm_builder, retval);
  }

  codegen_internal_leave_scope(codegen, 0);

  destroy_kv(codegen->locals);
  codegen->current_function = NULL;
  codegen->retval = NULL;
  codegen->return_block = NULL;
  codegen->current_function_metadata = NULL;
}
