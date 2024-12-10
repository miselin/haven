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
  LLVMContextRef context = LLVMGetGlobalContext();
  LLVMBasicBlockRef *arm_blocks = calloc(match->num_arms, sizeof(LLVMBasicBlockRef));
  LLVMValueRef otherwise_value = NULL;

  LLVMValueRef main_expr = emit_expr(codegen, match->expr);

  LLVMBasicBlockRef start_block = LLVMGetInsertBlock(codegen->llvm_builder);

  // add the phi node early so we can start adding incoming values
  LLVMBasicBlockRef end_block = LLVMAppendBasicBlock(codegen->current_function, "match.post");
  LLVMPositionBuilderAtEnd(codegen->llvm_builder, end_block);
  LLVMValueRef phi = LLVMBuildPhi(codegen->llvm_builder, ast_ty_to_llvm_ty(codegen, ty), "match");

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

  LLVMBasicBlockRef otherwise_block =
      LLVMAppendBasicBlock(codegen->current_function, "match.otherwise");
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

    LLVMValueRef expr = emit_expr(codegen, arm->expr);

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
