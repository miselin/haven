#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <llvm-c/Types.h>

#include "ast.h"
#include "codegen.h"
#include "compiler.h"
#include "internal.h"
#include "types.h"
#include "utility.h"

LLVMValueRef emit_if(struct codegen *codegen, struct ast_expr *ast) {
  compiler_log(codegen->compiler, LogLevelDebug, "codegen", "emit_if %p", (void *)ast);

  LLVMValueRef cond = emit_expr(codegen, ast->expr.if_expr.cond);
  LLVMTypeKind kind = LLVMGetTypeKind(LLVMTypeOf(cond));
  if (kind == LLVMIntegerTypeKind) {
    // is it already bool?
    if (LLVMGetIntTypeWidth(LLVMTypeOf(cond)) != 1) {
      cond = LLVMBuildICmp(
          codegen->llvm_builder, LLVMIntNE, cond,
          LLVMConstInt(ast_ty_to_llvm_ty(codegen, ast->expr.if_expr.cond->ty), 0, 0), "tobool");
    }
  }

  LLVMContextRef context = codegen->llvm_context;

  LLVMTypeRef expr_ty = ast_ty_to_llvm_ty(codegen, ast->ty);

  LLVMBasicBlockRef then_block = LLVMCreateBasicBlockInContext(context, "if.then");
  LLVMBasicBlockRef end_block = LLVMCreateBasicBlockInContext(context, "if.end");
  LLVMBasicBlockRef next_block =
      ast->expr.if_expr.elseifs ? LLVMCreateBasicBlockInContext(context, "if.elseif.cond") : NULL;
  LLVMBasicBlockRef else_block =
      ast->expr.if_expr.has_else ? LLVMCreateBasicBlockInContext(context, "if.else") : end_block;

  LLVMValueRef phi = NULL;
  if (ast->ty->ty != AST_TYPE_VOID) {
    LLVMBasicBlockRef start = LLVMGetInsertBlock(codegen->llvm_builder);
    LLVMAppendExistingBasicBlock(codegen->current_function, end_block);
    LLVMPositionBuilderAtEnd(codegen->llvm_builder, end_block);
    phi = LLVMBuildPhi(codegen->llvm_builder, expr_ty, "phi");

    LLVMPositionBuilderAtEnd(codegen->llvm_builder, start);
  }

  LLVMBasicBlockRef last_block = NULL;

  LLVMBuildCondBr(codegen->llvm_builder, cond, then_block, next_block ? next_block : else_block);

  LLVMAppendExistingBasicBlock(codegen->current_function, then_block);
  LLVMPositionBuilderAtEnd(codegen->llvm_builder, then_block);
  LLVMValueRef then_val = emit_block(codegen, &ast->expr.if_expr.then_block);
  last_block = LLVMGetInsertBlock(codegen->llvm_builder);
  if (phi) {
    LLVMAddIncoming(phi, &then_val, &last_block, 1);
  }
  if (!LLVMGetBasicBlockTerminator(last_block)) {
    LLVMBuildBr(codegen->llvm_builder, end_block);
  }

  if (ast->expr.if_expr.elseifs) {
    struct ast_expr_elseif *elseif = ast->expr.if_expr.elseifs;
    while (elseif) {
      LLVMBasicBlockRef this_block = next_block;
      next_block =
          elseif->next ? LLVMCreateBasicBlockInContext(context, "if.elseif.cond") : else_block;

      LLVMAppendExistingBasicBlock(codegen->current_function, this_block);
      LLVMPositionBuilderAtEnd(codegen->llvm_builder, this_block);

      LLVMBasicBlockRef cond_true_block =
          LLVMCreateBasicBlockInContext(context, "if.stmt.elseif.true");

      LLVMValueRef elseif_cond = emit_expr(codegen, elseif->cond);
      LLVMBuildCondBr(codegen->llvm_builder, elseif_cond, cond_true_block, next_block);

      LLVMAppendExistingBasicBlock(codegen->current_function, cond_true_block);
      LLVMPositionBuilderAtEnd(codegen->llvm_builder, cond_true_block);
      LLVMValueRef elseif_val = emit_block(codegen, &elseif->block);
      last_block = LLVMGetInsertBlock(codegen->llvm_builder);
      if (phi) {
        LLVMAddIncoming(phi, &elseif_val, &last_block, 1);
      }
      if (!LLVMGetBasicBlockTerminator(last_block)) {
        LLVMBuildBr(codegen->llvm_builder, end_block);
      }

      elseif = elseif->next;
    }
  }

  if (ast->expr.if_expr.has_else) {
    LLVMAppendExistingBasicBlock(codegen->current_function, else_block);
    LLVMPositionBuilderAtEnd(codegen->llvm_builder, else_block);
    LLVMValueRef else_val = emit_block(codegen, &ast->expr.if_expr.else_block);
    last_block = LLVMGetInsertBlock(codegen->llvm_builder);
    if (phi) {
      LLVMAddIncoming(phi, &else_val, &last_block, 1);
    }
    if (!LLVMGetBasicBlockTerminator(last_block)) {
      LLVMBuildBr(codegen->llvm_builder, end_block);
    }
  }

  if (phi) {
    LLVMMoveBasicBlockAfter(end_block, last_block);
  } else {
    LLVMAppendExistingBasicBlock(codegen->current_function, end_block);
  }

  LLVMPositionBuilderAtEnd(codegen->llvm_builder, end_block);

  return phi;
}
