#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <llvm-c/Types.h>
#include <stdlib.h>

#include "ast.h"
#include "codegen.h"
#include "internal.h"
#include "scope.h"
#include "types.h"
#include "utility.h"

LLVMValueRef emit_match_expr(struct codegen *codegen, struct ast_ty *ty,
                             struct ast_expr_match *match) {
  LLVMContextRef context = codegen->llvm_context;
  LLVMBasicBlockRef *arm_blocks = calloc(match->num_arms, sizeof(LLVMBasicBlockRef));
  LLVMValueRef otherwise_value = NULL;

  LLVMTypeRef result_ty = ast_ty_to_llvm_ty(codegen, ty);
  if (ty->ty == AST_TYPE_ENUM && !ty->oneof.enumty.no_wrapped_fields) {
    result_ty = LLVMPointerType(result_ty, 0);
  }

  LLVMValueRef main_expr = emit_expr(codegen, match->expr);

  // inner storage of an enum type, for pattern matches
  LLVMValueRef main_expr_buf = NULL;

  if (match->expr->ty->ty == AST_TYPE_ENUM && !match->expr->ty->oneof.enumty.no_wrapped_fields) {
    LLVMTypeRef enum_ty = ast_underlying_ty_to_llvm_ty(codegen, match->expr->ty);
    LLVMValueRef tag_ptr =
        LLVMBuildStructGEP2(codegen->llvm_builder, enum_ty, main_expr, 0, "tagptr");
    main_expr_buf = LLVMBuildStructGEP2(codegen->llvm_builder, enum_ty, main_expr, 1, "bufptr");

    // match the embedded tag instead of the object
    main_expr = LLVMBuildLoad2(codegen->llvm_builder, LLVMInt32TypeInContext(codegen->llvm_context),
                               tag_ptr, "tag");
  }

  LLVMBasicBlockRef otherwise_block = LLVMCreateBasicBlockInContext(context, "match.otherwise");
  LLVMBasicBlockRef end_block =
      LLVMAppendBasicBlockInContext(context, codegen->current_function, "match.post");

  LLVMBasicBlockRef current_block = LLVMGetInsertBlock(codegen->llvm_builder);
  LLVMPositionBuilderAtEnd(codegen->llvm_builder, end_block);
  LLVMValueRef phi =
      ty->ty == AST_TYPE_VOID ? NULL : LLVMBuildPhi(codegen->llvm_builder, result_ty, "match.phi");

  LLVMPositionBuilderAtEnd(codegen->llvm_builder, current_block);

  LLVMValueRef switch_value = LLVMBuildSwitch(codegen->llvm_builder, main_expr, otherwise_block,
                                              (unsigned int)match->num_arms);

  struct ast_expr_match_arm *arm = match->arms;
  for (size_t i = 0; i < match->num_arms; ++i) {
    arm_blocks[i] = LLVMCreateBasicBlockInContext(context, "match.arm");

    LLVMValueRef expr = emit_expr(codegen, arm->pattern);
    arm = arm->next;

    LLVMAddCase(switch_value, expr, arm_blocks[i]);
  }

  // emit the otherwise to run if all the conditions fail to match
  {
    LLVMAppendExistingBasicBlock(codegen->current_function, otherwise_block);
    LLVMPositionBuilderAtEnd(codegen->llvm_builder, otherwise_block);

    if (match->otherwise) {
      otherwise_value = emit_expr(codegen, match->otherwise->expr);
    } else {
      otherwise_value = NULL;
    }
    current_block = LLVMGetInsertBlock(codegen->llvm_builder);
    if (!LLVMGetBasicBlockTerminator(current_block)) {
      if (phi && otherwise_value) {
        LLVMAddIncoming(phi, &otherwise_value, &current_block, 1);
      }
      LLVMBuildBr(codegen->llvm_builder, end_block);
    }
  }

  // emit the arms now
  arm = match->arms;
  size_t i = 0;
  while (arm) {
    LLVMBasicBlockRef arm_block = arm_blocks[i];
    LLVMAppendExistingBasicBlock(codegen->current_function, arm_blocks[i]);
    LLVMPositionBuilderAtEnd(codegen->llvm_builder, arm_block);

    if (arm->pattern->type == AST_EXPR_TYPE_PATTERN_MATCH &&
        arm->pattern->expr.pattern_match.inner_vdecl) {
      codegen_internal_enter_scope(codegen, &arm->expr->loc, 1);

      struct ast_ty *binding_ty = arm->pattern->expr.pattern_match.inner_vdecl->ty;

      // we need to unwrap & define the inner value here
      struct scope_entry *entry = calloc(1, sizeof(struct scope_entry));
      entry->flags = arm->pattern->expr.pattern_match.inner_vdecl->flags;
      entry->variable_type = ast_ty_to_llvm_ty(codegen, binding_ty);
      if (type_is_complex(binding_ty)) {
        entry->ref = main_expr_buf;
      } else {
        entry->ref =
            LLVMBuildLoad2(codegen->llvm_builder, entry->variable_type, main_expr_buf, "inner");
      }
      scope_insert(codegen->scope,
                   arm->pattern->expr.pattern_match.inner_vdecl->ident.value.identv.ident, entry);
    }

    LLVMValueRef expr = emit_expr(codegen, arm->expr);
    if (phi) {
      current_block = LLVMGetInsertBlock(codegen->llvm_builder);
      LLVMAddIncoming(phi, &expr, &current_block, 1);
    }

    if (arm->pattern->type == AST_EXPR_TYPE_PATTERN_MATCH &&
        arm->pattern->expr.pattern_match.inner_vdecl) {
      codegen_internal_leave_scope(codegen, 1);
    }

    current_block = LLVMGetInsertBlock(codegen->llvm_builder);
    if (!LLVMGetBasicBlockTerminator(current_block)) {
      LLVMBuildBr(codegen->llvm_builder, end_block);
    }

    arm = arm->next;
    ++i;
  }

  current_block = LLVMGetInsertBlock(codegen->llvm_builder);
  LLVMMoveBasicBlockAfter(end_block, current_block);

  LLVMPositionBuilderAtEnd(codegen->llvm_builder, end_block);

  free(arm_blocks);

  return phi;
}
