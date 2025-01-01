#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <llvm-c/DebugInfo.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Types.h>
#include <stdlib.h>
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
  char mangled[1024];
  codegen_mangle(codegen, fdecl, mangled, 1024);

  const char *ident = fdecl->ident.value.identv.ident;

  compiler_log(codegen->compiler, LogLevelDebug, "codegen", "emit fdecl %s (as %s)", ident,
               mangled);

  if (fdecl->function_ty->function.retty->specialization_of) {
    // we need to create the return type as a new global
    emit_enum_type(codegen, fdecl->function_ty->function.retty);
  }

  LLVMValueRef func = NULL;
  LLVMTypeRef ret_ty = ast_underlying_ty_to_llvm_ty(codegen, fdecl->function_ty->function.retty);
  LLVMTypeRef orig_ret_ty = ret_ty;
  LLVMTypeRef *param_types = NULL;

  int rc = type_is_complex(fdecl->function_ty->function.retty);
  if (rc < 0) {
    fprintf(stderr, "type_is_complex returned a completely unexpected value %d\n", rc);
    return;
  }

  size_t complex_return = (size_t)rc;
  size_t num_params = fdecl->num_params + complex_return;

  if (complex_return) {
    ret_ty = LLVMVoidTypeInContext(codegen->llvm_context);
  }

  struct scope_entry *entry = scope_lookup(codegen->scope, ident, 0);
  if (!entry) {
    // emit declaration
    param_types = malloc(sizeof(LLVMTypeRef) * num_params);
    if (complex_return) {
      param_types[0] = LLVMPointerTypeInContext(codegen->llvm_context, 0);
    }
    for (size_t i = 0; i < fdecl->num_params; i++) {
      param_types[i + complex_return] =
          ast_underlying_ty_to_llvm_ty(codegen, fdecl->function_ty->function.param_types[i]);
    }
    LLVMTypeRef func_type = LLVMFunctionType(ret_ty, param_types, (unsigned int)num_params,
                                             fdecl->flags & DECL_FLAG_VARARG);
    if (!fdecl->is_intrinsic) {
      func = LLVMAddFunction(codegen->llvm_module, mangled, func_type);
      if (fdecl->flags & DECL_FLAG_PUB) {
        LLVMSetLinkage(func, LLVMExternalLinkage);
      } else {
        LLVMSetLinkage(func, LLVMInternalLinkage);
      }

      if (complex_return) {
        LLVMAddAttributeAtIndex(func, 1, codegen_type_attribute(codegen, "sret", orig_ret_ty));
      }

      if (compiler_get_flags(codegen->compiler) & FLAG_ASAN) {
        LLVMAddAttributeAtIndex(func, (LLVMAttributeIndex)LLVMAttributeFunctionIndex,
                                codegen_enum_attribute(codegen, "sanitize_address", 0));
      }
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
    }

    entry = calloc(1, sizeof(struct scope_entry));
    entry->function_type = func_type;
    entry->param_types = param_types;
    entry->ref = func;
    scope_insert(codegen->scope, ident, entry);
  } else {
    func = entry->ref;
    param_types = entry->param_types;
  }

  if (fdecl->is_intrinsic) {
    LLVMTypeRef *intrinsic_types =
        fdecl->num_intrinsic_tys ? malloc(sizeof(LLVMTypeRef) * fdecl->num_intrinsic_tys) : NULL;
    for (size_t i = 0; i < fdecl->num_intrinsic_tys; i++) {
      intrinsic_types[i] = ast_ty_to_llvm_ty(codegen, &fdecl->intrinsic_tys[i]);
    }
    LLVMTypeRef intrinsic_ftype = NULL;
    entry->ref = build_intrinsic2(codegen, fdecl->intrinsic, &intrinsic_ftype, intrinsic_types,
                                  fdecl->num_intrinsic_tys);
    if (!entry->ref) {
      fprintf(stderr, "failed to build intrinsic %s\n", fdecl->intrinsic);
      return;
    }
    entry->function_type = intrinsic_ftype;

    if (intrinsic_types) {
      free(intrinsic_types);
    }

    return;
  }

  // generate definition if we have one
  if (!fdecl->body) {
    return;
  }

  {
    struct codegen_compileunit *unit = codegen_get_compileunit(codegen, at);

    LLVMMetadataRef func_md =
        LLVMDIBuilderCreateSubroutineType(codegen->llvm_dibuilder, unit->file_metadata, NULL, 0, 0);

    LLVMMetadataRef function_metadata = LLVMDIBuilderCreateFunction(
        codegen->llvm_dibuilder, unit->file_metadata, mangled, strlen(mangled), "", 0,
        unit->file_metadata, (unsigned)at->line + 1, func_md, (fdecl->flags & DECL_FLAG_PUB) == 0,
        fdecl->body != NULL, 1, 0, 0);

    LLVMSetSubprogram(func, function_metadata);

    codegen->current_function_metadata = function_metadata;
    update_debug_loc(codegen, at);
  }

  LLVMContextRef context = codegen->llvm_context;
  LLVMBasicBlockRef defers = LLVMCreateBasicBlockInContext(context, "defers");
  LLVMBasicBlockRef box_derefs = LLVMCreateBasicBlockInContext(context, "box.derefs");
  LLVMBasicBlockRef return_block = LLVMCreateBasicBlockInContext(context, "return");
  codegen->return_block = defers;

  codegen->entry_block = LLVMAppendBasicBlockInContext(context, func, "entry");
  codegen->last_alloca = NULL;
  LLVMPositionBuilderAtEnd(codegen->llvm_builder, codegen->entry_block);

  if (complex_return) {
    codegen->retval = LLVMGetParam(func, 0);
  } else if (fdecl->function_ty->function.retty->ty != AST_TYPE_VOID) {
    codegen->retval = new_alloca(codegen, ret_ty, "retval");
  } else {
    codegen->retval = NULL;
  }

  codegen->current_function = func;
  codegen->locals = new_kv();

  codegen->defer_head = NULL;
  codegen->boxes = NULL;

  codegen_internal_enter_scope(codegen, at, 0);

  for (size_t i = 0; i < fdecl->num_params; i++) {
    const char *param_ident = fdecl->params[i].name;
    struct scope_entry *param_entry = calloc(1, sizeof(struct scope_entry));
    param_entry->ty = fdecl->function_ty->function.param_types[i];
    param_entry->variable_type = param_types[i + complex_return];
    param_entry->ref = LLVMGetParam(func, (unsigned int)(i + complex_return));
    param_entry->flags = fdecl->params[i].flags;
    scope_insert(codegen->scope, param_ident, param_entry);

    if (param_entry->ty->ty == AST_TYPE_BOX) {
      compiler_log(codegen->compiler, LogLevelDebug, "codegen", "param %s is a box", param_ident);
      // need to store this param on the stack
      LLVMValueRef param = new_alloca(codegen, codegen_pointer_type(codegen), param_ident);
      LLVMBuildStore(codegen->llvm_builder, param_entry->ref, param);
      param_entry->ref = param;

      struct box_entry *box = calloc(1, sizeof(struct box_entry));
      box->box = param;
      box->next = codegen->boxes;
      codegen->boxes = box;
    }
  }

  LLVMValueRef block_result = emit_block(codegen, fdecl->body);
  if (fdecl->function_ty->function.retty->ty == AST_TYPE_BOX) {
    // ref the box we're returning
    compiler_log(codegen->compiler, LogLevelDebug, "codegen", "box ref due to return");
    codegen_box_ref(codegen, block_result, 0);

    // load the box out of the return value
    block_result = LLVMBuildLoad2(codegen->llvm_builder, codegen_pointer_type(codegen),
                                  block_result, "box.into.retval");
  }

  if (fdecl->function_ty->function.retty->ty != AST_TYPE_VOID) {
    // complex_return path sets retval to the return parameter
    emit_store(codegen, fdecl->function_ty->function.retty, block_result, codegen->retval);
  }

  LLVMBuildBr(codegen->llvm_builder, defers);

  LLVMAppendExistingBasicBlock(codegen->current_function, defers);
  LLVMPositionBuilderAtEnd(codegen->llvm_builder, defers);

  LLVMBasicBlockRef next_block = box_derefs;

  // run defer expressions, if any
  struct defer_entry *defer = codegen->defer_head;
  LLVMBuildBr(codegen->llvm_builder, defer ? defer->llvm_block : next_block);

  while (defer) {
    LLVMMoveBasicBlockAfter(defer->llvm_block, LLVMGetInsertBlock(codegen->llvm_builder));
    LLVMPositionBuilderAtEnd(codegen->llvm_builder, defer->llvm_block_after);
    struct defer_entry *next = defer->next;
    LLVMBuildBr(codegen->llvm_builder, next ? next->llvm_block : next_block);
    free(defer);
    defer = next;
  }

  LLVMAppendExistingBasicBlock(codegen->current_function, box_derefs);
  LLVMPositionBuilderAtEnd(codegen->llvm_builder, box_derefs);

  // run box derefs, if any
  struct box_entry *box = codegen->boxes;
  while (box) {
    codegen_box_unref(codegen, box->box, 0);

    struct box_entry *next = box->next;
    free(box);
    box = next;
  }

  LLVMBuildBr(codegen->llvm_builder, return_block);

  // insert return block finally
  LLVMAppendExistingBasicBlock(codegen->current_function, return_block);
  LLVMPositionBuilderAtEnd(codegen->llvm_builder, return_block);

  if (fdecl->function_ty->function.retty->ty == AST_TYPE_VOID || complex_return) {
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
  codegen->boxes = NULL;
}
