#include <llvm-c-18/llvm-c/Types.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>

#include "ast.h"
#include "codegen.h"
#include "internal.h"
#include "utility.h"

LLVMValueRef emit_if(struct codegen *codegen, struct ast_expr *ast) {
  LLVMValueRef cond_expr = emit_expr(codegen, ast->if_expr.cond);
  LLVMValueRef cond = LLVMBuildICmp(
      codegen->llvm_builder, LLVMIntNE, cond_expr,
      LLVMConstInt(ast_ty_to_llvm_ty(codegen, &ast->if_expr.cond->ty), 0, 0), "tobool");

  LLVMContextRef context = codegen->llvm_context;

  LLVMBasicBlockRef then_block = LLVMCreateBasicBlockInContext(context, "if.expr.then");
  LLVMBasicBlockRef end_block = LLVMCreateBasicBlockInContext(context, "if.expr.end");
  LLVMBasicBlockRef else_block =
      ast->if_expr.has_else ? LLVMCreateBasicBlockInContext(context, "if.expr.else") : end_block;

  LLVMBuildCondBr(codegen->llvm_builder, cond, then_block, else_block);

  LLVMAppendExistingBasicBlock(codegen->current_function, then_block);
  LLVMPositionBuilderAtEnd(codegen->llvm_builder, then_block);
  LLVMValueRef then_val = emit_block(codegen, &ast->if_expr.then_block);
  LLVMBasicBlockRef final_then_block = LLVMGetInsertBlock(codegen->llvm_builder);
  LLVMBuildBr(codegen->llvm_builder, end_block);

  LLVMAppendExistingBasicBlock(codegen->current_function, else_block);
  LLVMPositionBuilderAtEnd(codegen->llvm_builder, else_block);
  LLVMValueRef else_val = emit_block(codegen, &ast->if_expr.else_block);
  LLVMBasicBlockRef final_else_block = LLVMGetInsertBlock(codegen->llvm_builder);
  LLVMBuildBr(codegen->llvm_builder, end_block);

  LLVMAppendExistingBasicBlock(codegen->current_function, end_block);
  LLVMPositionBuilderAtEnd(codegen->llvm_builder, end_block);
  LLVMValueRef phi =
      LLVMBuildPhi(codegen->llvm_builder, ast_ty_to_llvm_ty(codegen, &ast->ty), "phi");
  LLVMValueRef values[] = {then_val, else_val};
  LLVMBasicBlockRef blocks[] = {final_then_block, final_else_block};
  LLVMAddIncoming(phi, values, blocks, 2);

  return phi;
}

void emit_void_if(struct codegen *codegen, struct ast_expr *ast) {
  LLVMValueRef cond_expr = emit_expr(codegen, ast->if_expr.cond);
  LLVMValueRef cond = LLVMBuildICmp(
      codegen->llvm_builder, LLVMIntNE, cond_expr,
      LLVMConstInt(ast_ty_to_llvm_ty(codegen, &ast->if_expr.cond->ty), 0, 0), "tobool");

  LLVMContextRef context = codegen->llvm_context;

  LLVMBasicBlockRef then_block = LLVMCreateBasicBlockInContext(context, "if.stmt.then");
  LLVMBasicBlockRef end_block = LLVMCreateBasicBlockInContext(context, "if.stmt.end");
  LLVMBasicBlockRef else_block =
      ast->if_expr.has_else ? LLVMCreateBasicBlockInContext(context, "if.stmt.else") : end_block;

  LLVMBuildCondBr(codegen->llvm_builder, cond, then_block, end_block);

  LLVMAppendExistingBasicBlock(codegen->current_function, then_block);
  LLVMPositionBuilderAtEnd(codegen->llvm_builder, then_block);
  emit_block(codegen, &ast->if_expr.then_block);

  if (!LLVMGetBasicBlockTerminator(then_block)) {
    LLVMBuildBr(codegen->llvm_builder, end_block);
  }

  if (ast->if_expr.has_else) {
    LLVMAppendExistingBasicBlock(codegen->current_function, else_block);
    LLVMPositionBuilderAtEnd(codegen->llvm_builder, else_block);
    emit_block(codegen, &ast->if_expr.else_block);
    if (!LLVMGetBasicBlockTerminator(else_block)) {
      LLVMBuildBr(codegen->llvm_builder, end_block);
    }
  }

  // handle missing terminator in the current block (which might not be the then/else block)
  if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(codegen->llvm_builder))) {
    LLVMBuildBr(codegen->llvm_builder, end_block);
  }

  LLVMAppendExistingBasicBlock(codegen->current_function, end_block);
  LLVMPositionBuilderAtEnd(codegen->llvm_builder, end_block);
}
