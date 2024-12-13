#include <llvm-c-18/llvm-c/Types.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <malloc.h>

#include "ast.h"
#include "codegen.h"
#include "internal.h"
#include "scope.h"
#include "utility.h"

LLVMValueRef emit_match_expr(struct codegen *codegen, struct ast_ty *ty,
                             struct ast_expr_match *match) {
  LLVMContextRef context = codegen->llvm_context;
  LLVMBasicBlockRef *arm_blocks = calloc(match->num_arms, sizeof(LLVMBasicBlockRef));
  LLVMValueRef otherwise_value = NULL;

  LLVMValueRef main_expr = emit_expr(codegen, match->expr);

  LLVMTypeRef main_expr_ty = ast_ty_to_llvm_ty(codegen, &match->expr->ty);

  // inner storage of an enum type, for pattern matches
  LLVMValueRef main_expr_buf = NULL;

  if (match->expr->ty.ty == AST_TYPE_ENUM && !match->expr->ty.enumty.no_wrapped_fields) {
    LLVMValueRef tag_ptr =
        LLVMBuildStructGEP2(codegen->llvm_builder, main_expr_ty, main_expr, 0, "tagptr");
    main_expr_buf =
        LLVMBuildStructGEP2(codegen->llvm_builder, main_expr_ty, main_expr, 1, "bufptr");

    // match the embedded tag instead of the object
    main_expr = LLVMBuildLoad2(codegen->llvm_builder, LLVMInt32Type(), tag_ptr, "tag");
  }

  LLVMBasicBlockRef start_block = LLVMGetInsertBlock(codegen->llvm_builder);

  LLVMTypeRef phi_ty = ast_ty_to_llvm_ty(codegen, ty);
  if (ty->ty == AST_TYPE_ENUM && !ty->enumty.no_wrapped_fields) {
    phi_ty = LLVMPointerType(phi_ty, 0);
  }

  // add the phi node early so we can start adding incoming values
  LLVMBasicBlockRef end_block =
      LLVMAppendBasicBlockInContext(codegen->llvm_context, codegen->current_function, "match.post");
  LLVMPositionBuilderAtEnd(codegen->llvm_builder, end_block);
  LLVMValueRef phi = LLVMBuildPhi(codegen->llvm_builder, phi_ty, "match.result");

  LLVMPositionBuilderAtEnd(codegen->llvm_builder, start_block);

  struct ast_expr_match_arm *arm = match->arms;
  for (size_t i = 0; i < match->num_arms; ++i) {
    arm_blocks[i] = LLVMCreateBasicBlockInContext(context, "match.arm");

    LLVMValueRef expr = emit_expr(codegen, arm->pattern);
    LLVMValueRef comp = LLVMBuildICmp(codegen->llvm_builder, LLVMIntEQ, main_expr, expr, "comp");
    arm = arm->next;

    LLVMBasicBlockRef after_cond = LLVMCreateBasicBlockInContext(context, "");
    LLVMBuildCondBr(codegen->llvm_builder, comp, arm_blocks[i], after_cond);

    LLVMAppendExistingBasicBlock(codegen->current_function, after_cond);
    LLVMPositionBuilderAtEnd(codegen->llvm_builder, after_cond);
  }

  LLVMBasicBlockRef otherwise_block = LLVMAppendBasicBlockInContext(
      codegen->llvm_context, codegen->current_function, "match.otherwise");
  LLVMBuildBr(codegen->llvm_builder, otherwise_block);

  // emit the otherwise to run if all the conditions fail to match
  {
    LLVMAppendExistingBasicBlock(codegen->current_function, otherwise_block);
    LLVMPositionBuilderAtEnd(codegen->llvm_builder, otherwise_block);

    otherwise_value = emit_expr(codegen, match->otherwise->expr);
    LLVMBasicBlockRef current = LLVMGetInsertBlock(codegen->llvm_builder);
    LLVMAddIncoming(phi, &otherwise_value, &current, 1);

    LLVMBuildBr(codegen->llvm_builder, end_block);
  }

  // emit the arms now
  arm = match->arms;
  size_t i = 0;
  while (arm) {
    LLVMBasicBlockRef arm_block = arm_blocks[i];
    LLVMAppendExistingBasicBlock(codegen->current_function, arm_blocks[i]);
    LLVMPositionBuilderAtEnd(codegen->llvm_builder, arm_block);

    if (arm->pattern->type == AST_EXPR_TYPE_PATTERN_MATCH &&
        !arm->pattern->pattern_match.is_wildcard) {
      fprintf(stderr, "processing pattern match %p [%d]\n", (void *)arm->pattern,
              arm->pattern->pattern_match.is_wildcard);
      codegen_internal_enter_scope(codegen, &arm->expr->loc, 1);

      // we need to unwrap & define the inner value here
      struct scope_entry *entry = calloc(1, sizeof(struct scope_entry));
      entry->vdecl = calloc(1, sizeof(struct ast_vdecl));
      entry->vdecl->ident = arm->pattern->pattern_match.inner;
      entry->vdecl->ty = arm->pattern->pattern_match.inner_ty;
      entry->vdecl->flags = DECL_FLAG_TEMPORARY;
      entry->variable_type = ast_ty_to_llvm_ty(codegen, &arm->pattern->pattern_match.inner_ty);
      /*entry->ref = LLVMBuildAlloca(codegen->llvm_builder, entry->variable_type,
                                   arm->pattern->pattern_match.inner.value.identv.ident); */
      entry->ref =
          LLVMBuildLoad2(codegen->llvm_builder, entry->variable_type, main_expr_buf, "inner");
      scope_insert(codegen->scope, arm->pattern->pattern_match.inner.value.identv.ident, entry);
      fprintf(stderr, "defined %s in the inner scope of this match\n",
              arm->pattern->pattern_match.inner.value.identv.ident);
    }

    LLVMValueRef expr = emit_expr(codegen, arm->expr);

    if (arm->pattern->ty.ty == AST_EXPR_TYPE_PATTERN_MATCH &&
        !arm->pattern->pattern_match.is_wildcard) {
      codegen_internal_leave_scope(codegen, 1);
    }

    LLVMBasicBlockRef current = LLVMGetInsertBlock(codegen->llvm_builder);
    LLVMAddIncoming(phi, &expr, &current, 1);

    LLVMBuildBr(codegen->llvm_builder, end_block);
    arm = arm->next;
    ++i;
  }

  LLVMMoveBasicBlockAfter(end_block, LLVMGetInsertBlock(codegen->llvm_builder));
  LLVMPositionBuilderAtEnd(codegen->llvm_builder, end_block);

  free(arm_blocks);

  return phi;
}
