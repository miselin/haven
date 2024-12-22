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
      unsigned int kind = LLVMGetEnumAttributeKindForName("sret", 4);
      LLVMAttributeRef attr = LLVMCreateTypeAttribute(codegen->llvm_context, kind, orig_ret_ty);
      LLVMAddAttributeAtIndex(func, 1, attr);
    }

    {
      unsigned int sanitize = LLVMGetEnumAttributeKindForName("sanitize_address", 16);
      unsigned int memory = LLVMGetEnumAttributeKindForName("memory", 6);
      unsigned int nounwind = LLVMGetEnumAttributeKindForName("nounwind", 8);

      const char *frameptr_name = "frame-pointer";
      LLVMAttributeRef frameptr_attr = LLVMCreateStringAttribute(
          codegen->llvm_context, frameptr_name, (unsigned)strlen(frameptr_name), "all",
          (unsigned)strlen("all"));

      // sanitize_address for ASAN - need cli flags for this
      LLVMContextRef context = codegen->llvm_context;
      LLVMAttributeRef sanitize_attr = LLVMCreateEnumAttribute(context, sanitize, 0);
      LLVMAddAttributeAtIndex(func, (LLVMAttributeIndex)LLVMAttributeFunctionIndex, sanitize_attr);
      LLVMAttributeRef nounwind_attr = LLVMCreateEnumAttribute(context, nounwind, 0);
      LLVMAddAttributeAtIndex(func, (LLVMAttributeIndex)LLVMAttributeFunctionIndex, nounwind_attr);
      LLVMAddAttributeAtIndex(func, (LLVMAttributeIndex)LLVMAttributeFunctionIndex, frameptr_attr);

      {
        const char *attr_name = "target-cpu";
        const char *attr_value = "x86-64";
        LLVMAttributeRef attr =
            LLVMCreateStringAttribute(codegen->llvm_context, attr_name, (unsigned)strlen(attr_name),
                                      attr_value, (unsigned)strlen(attr_value));
        LLVMAddAttributeAtIndex(func, (LLVMAttributeIndex)LLVMAttributeFunctionIndex, attr);
      }
      {
        const char *attr_name = "tune-cpu";
        const char *attr_value = "generic";
        LLVMAttributeRef attr =
            LLVMCreateStringAttribute(codegen->llvm_context, attr_name, (unsigned)strlen(attr_name),
                                      attr_value, (unsigned)strlen(attr_value));
        LLVMAddAttributeAtIndex(func, (LLVMAttributeIndex)LLVMAttributeFunctionIndex, attr);
      }
      {
        const char *attr_name = "target-features";
        const char *attr_value = LLVMGetHostCPUFeatures();
        LLVMAttributeRef attr =
            LLVMCreateStringAttribute(codegen->llvm_context, attr_name, (unsigned)strlen(attr_name),
                                      attr_value, (unsigned)strlen(attr_value));
        LLVMAddAttributeAtIndex(func, (LLVMAttributeIndex)LLVMAttributeFunctionIndex, attr);
        LLVMDisposeMessage((char *)attr_value);
      }

      LLVMAttributeRef mem_attr = NULL;
      if ((fdecl->flags & DECL_FLAG_IMPURE) == 0) {
        // argmem: readwrite (allow struct returns to be pure)
        mem_attr = LLVMCreateEnumAttribute(context, memory, 3);
      } else {
        // allow all types of access
        mem_attr = LLVMCreateEnumAttribute(context, memory, ~0U);
      }
      LLVMAddAttributeAtIndex(func, (LLVMAttributeIndex)LLVMAttributeFunctionIndex, mem_attr);
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
    emit_store(codegen, &fdecl->retty, block_result, codegen->retval);
  }

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

  LLVMBuildBr(codegen->llvm_builder, return_block);

  // insert return block finally
  LLVMAppendExistingBasicBlock(codegen->current_function, return_block);
  LLVMPositionBuilderAtEnd(codegen->llvm_builder, return_block);

  if (fdecl->retty.ty == AST_TYPE_VOID || complex_return) {
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
