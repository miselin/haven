#include <llvm-c-18/llvm-c/DebugInfo.h>
#include <llvm-c-18/llvm-c/TargetMachine.h>
#include <llvm-c-18/llvm-c/Types.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <malloc.h>
#include <string.h>

#include "ast.h"
#include "codegen.h"
#include "compiler.h"
#include "internal.h"
#include "kv.h"
#include "lex.h"
#include "scope.h"
#include "types.h"
#include "utility.h"

void emit_fdecl(struct codegen *codegen, struct ast_fdecl *fdecl, struct lex_locator *at) {
  compiler_log(codegen->compiler, LogLevelDebug, "codegen", "emit fdecl %s",
               fdecl->ident.value.identv.ident);

  if (fdecl->retty.specialization_of) {
    // we need to create the return type as a new global
    emit_enum_type(codegen, &fdecl->retty);
  }

  LLVMValueRef func = NULL;
  LLVMTypeRef ret_ty = ast_ty_to_llvm_ty(codegen, &fdecl->retty);
  LLVMTypeRef orig_ret_ty = ret_ty;
  LLVMTypeRef *param_types = NULL;

  int rc = type_is_complex(&fdecl->retty);
  if (rc < 0) {
    fprintf(stderr, "type_is_complex returned a completely unexpected value %d\n", rc);
    return;
  }

  size_t complex_return = (size_t)rc;
  size_t num_params = fdecl->num_params + complex_return;

  if (complex_return) {
    ret_ty = LLVMVoidTypeInContext(codegen->llvm_context);
  }

  struct scope_entry *entry = scope_lookup(codegen->scope, fdecl->ident.value.identv.ident, 0);
  if (!entry) {
    // emit declaration
    param_types = malloc(sizeof(LLVMTypeRef) * num_params);
    if (complex_return) {
      param_types[0] = LLVMPointerTypeInContext(codegen->llvm_context, 0);
    }
    for (size_t i = 0; i < fdecl->num_params; i++) {
      param_types[i + complex_return] = ast_ty_to_llvm_ty(codegen, &fdecl->params[i]->ty);
    }
    LLVMTypeRef func_type = LLVMFunctionType(ret_ty, param_types, (unsigned int)num_params,
                                             fdecl->flags & DECL_FLAG_VARARG);
    func = LLVMAddFunction(codegen->llvm_module, fdecl->ident.value.identv.ident, func_type);
    if (fdecl->flags & DECL_FLAG_PUB) {
      LLVMSetLinkage(func, LLVMExternalLinkage);
    } else {
      LLVMSetLinkage(func, LLVMInternalLinkage);
    }

    if (complex_return) {
      LLVMAddAttributeAtIndex(func, 1, codegen_type_attribute(codegen, "sret", orig_ret_ty));
    }

    // TODO: asan/msan/* flag
    LLVMAddAttributeAtIndex(func, (LLVMAttributeIndex)LLVMAttributeFunctionIndex,
                            codegen_enum_attribute(codegen, "sanitize_address", 0));
    LLVMAddAttributeAtIndex(func, (LLVMAttributeIndex)LLVMAttributeFunctionIndex,
                            codegen_enum_attribute(codegen, "nounwind", 0));
    // TODO: frame pointer flag
    LLVMAddAttributeAtIndex(func, (LLVMAttributeIndex)LLVMAttributeFunctionIndex,
                            codegen_string_attribute(codegen, "frame-pointer", "all"));

    char *cpu_name = LLVMGetHostCPUName();
    LLVMAddTargetDependentFunctionAttr(func, "target-cpu", cpu_name);
    LLVMDisposeMessage(cpu_name);

    LLVMAddTargetDependentFunctionAttr(func, "tune-cpu", "generic");
    char *features = LLVMGetHostCPUFeatures();
    LLVMAddTargetDependentFunctionAttr(func, "target-features", features);
    LLVMDisposeMessage(features);

    LLVMAttributeRef mem_attr = NULL;
    if ((fdecl->flags & DECL_FLAG_IMPURE) == 0) {
      // argmem: readwrite (allow struct returns to be pure)
      mem_attr = codegen_enum_attribute(codegen, "memory", 3);
    } else {
      // allow all types of access
      mem_attr = codegen_enum_attribute(codegen, "memory", ~0U);
    }
    LLVMAddAttributeAtIndex(func, (LLVMAttributeIndex)LLVMAttributeFunctionIndex, mem_attr);

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
  LLVMBasicBlockRef defers = LLVMCreateBasicBlockInContext(context, "defers");
  LLVMBasicBlockRef return_block = LLVMCreateBasicBlockInContext(context, "return");
  codegen->return_block = defers;

  codegen->entry_block = LLVMAppendBasicBlockInContext(context, func, "entry");
  codegen->last_alloca = NULL;
  LLVMPositionBuilderAtEnd(codegen->llvm_builder, codegen->entry_block);

  if (complex_return) {
    codegen->retval = LLVMGetParam(func, 0);
  } else if (fdecl->retty.ty != AST_TYPE_VOID) {
    codegen->retval = new_alloca(codegen, ret_ty, "retval");
  } else {
    codegen->retval = NULL;
  }

  codegen->current_function = func;
  codegen->locals = new_kv();

  codegen->defer_head = NULL;

  codegen_internal_enter_scope(codegen, at, 0);

  for (size_t i = 0; i < fdecl->num_params; i++) {
    struct scope_entry *param_entry = calloc(1, sizeof(struct scope_entry));
    param_entry->vdecl = fdecl->params[i];
    param_entry->variable_type = param_types[i + complex_return];
    param_entry->ref = LLVMGetParam(func, (unsigned int)(i + complex_return));
    scope_insert(codegen->scope, fdecl->params[i]->ident.value.identv.ident, param_entry);
  }

  LLVMValueRef block_result = emit_block(codegen, fdecl->body);
  if (fdecl->retty.ty != AST_TYPE_VOID) {
    // complex_return path sets retval to the return parameter
    /*
    if (fdecl->retty.flags == TYPE_FLAG_PTR && !LLVMIsNull(block_result)) {
      block_result =
          LLVMBuildLoad2(codegen->llvm_builder, ret_ty, block_result, "deref.ptr.retval");
    }
    */
    emit_store(codegen, &fdecl->retty, block_result, codegen->retval);
  }

  LLVMBuildBr(codegen->llvm_builder, defers);

  LLVMAppendExistingBasicBlock(codegen->current_function, defers);
  LLVMPositionBuilderAtEnd(codegen->llvm_builder, defers);

  // run defer expressions, if any
  struct defer_entry *defer = codegen->defer_head;
  LLVMBuildBr(codegen->llvm_builder, defer ? defer->llvm_block : return_block);

  while (defer) {
    LLVMMoveBasicBlockAfter(defer->llvm_block, LLVMGetInsertBlock(codegen->llvm_builder));
    LLVMPositionBuilderAtEnd(codegen->llvm_builder, defer->llvm_block_after);
    struct defer_entry *next = defer->next;
    LLVMBuildBr(codegen->llvm_builder, next ? next->llvm_block : return_block);
    free(defer);
    defer = next;
  }

  // insert return block finally
  LLVMAppendExistingBasicBlock(codegen->current_function, return_block);
  LLVMPositionBuilderAtEnd(codegen->llvm_builder, return_block);

  if (fdecl->retty.ty == AST_TYPE_VOID || complex_return) {
    LLVMBuildRetVoid(codegen->llvm_builder);
  } else {
    LLVMValueRef retval =
        LLVMBuildLoad2(codegen->llvm_builder, ret_ty, codegen->retval, "load.retval");
    LLVMBuildRet(codegen->llvm_builder, retval);
  }

  codegen_internal_leave_scope(codegen, 0);

  destroy_kv(codegen->locals);
  codegen->current_function = NULL;
  codegen->retval = NULL;
  codegen->return_block = NULL;
  codegen->current_function_metadata = NULL;
}
