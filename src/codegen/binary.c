#include <llvm-c-18/llvm-c/Types.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>

#include "ast.h"
#include "codegen.h"
#include "internal.h"
#include "utility.h"

LLVMValueRef emit_logical_expr(struct codegen *codegen, struct ast_expr_binary *binary,
                               struct ast_ty *ty) {
  UNUSED(ty);

  LLVMTypeRef lhs_type = ast_ty_to_llvm_ty(&binary->lhs->ty);
  LLVMTypeRef rhs_type = ast_ty_to_llvm_ty(&binary->rhs->ty);

  LLVMValueRef lhs = emit_expr(codegen, binary->lhs);
  LLVMValueRef lcmp =
      LLVMBuildICmp(codegen->llvm_builder, LLVMIntNE, lhs, LLVMConstInt(lhs_type, 0, 0), "lhs");

  LLVMContextRef context = LLVMGetGlobalContext();

  LLVMBasicBlockRef start = LLVMGetInsertBlock(codegen->llvm_builder);
  LLVMBasicBlockRef rhs = LLVMCreateBasicBlockInContext(context, "logic.rhs");
  LLVMBasicBlockRef end = LLVMCreateBasicBlockInContext(context, "logic.end");

  LLVMBuildCondBr(codegen->llvm_builder, lcmp, rhs, end);

  LLVMAppendExistingBasicBlock(codegen->current_function, rhs);
  LLVMPositionBuilderAtEnd(codegen->llvm_builder, rhs);
  LLVMValueRef rhs_val = emit_expr(codegen, binary->rhs);
  LLVMValueRef rcmp =
      LLVMBuildICmp(codegen->llvm_builder, LLVMIntNE, rhs_val, LLVMConstInt(rhs_type, 0, 0), "rhs");
  LLVMBasicBlockRef final_rhs = LLVMGetInsertBlock(codegen->llvm_builder);
  LLVMBuildBr(codegen->llvm_builder, end);

  LLVMAppendExistingBasicBlock(codegen->current_function, end);
  LLVMPositionBuilderAtEnd(codegen->llvm_builder, end);
  LLVMValueRef phi = LLVMBuildPhi(codegen->llvm_builder, LLVMInt1Type(), "phi");
  LLVMValueRef values[] = {LLVMConstInt(LLVMInt1Type(), 0, 0), rcmp};
  LLVMBasicBlockRef blocks[] = {start, final_rhs};
  LLVMAddIncoming(phi, values, blocks, 2);

  return phi;
}

LLVMValueRef emit_boolean_expr(struct codegen *codegen, struct ast_expr_binary *binary) {
  LLVMIntPredicate iop;
  LLVMRealPredicate fop;
  switch (binary->op) {
    case AST_BINARY_OP_EQUAL:
      iop = LLVMIntEQ;
      fop = LLVMRealOEQ;
      break;
    case AST_BINARY_OP_NOT_EQUAL:
      iop = LLVMIntNE;
      fop = LLVMRealONE;
      break;
    case AST_BINARY_OP_LT:
      iop = LLVMIntSLT;
      fop = LLVMRealOLT;
      break;
    case AST_BINARY_OP_GT:
      iop = LLVMIntSGT;
      fop = LLVMRealOGT;
      break;
    case AST_BINARY_OP_LTE:
      iop = LLVMIntSLE;
      fop = LLVMRealOLE;
      break;
    case AST_BINARY_OP_GTE:
      iop = LLVMIntSGE;
      fop = LLVMRealOGE;
      break;
    default:
      fprintf(stderr, "unhandled boolean operator %s\n", ast_binary_op_to_str(binary->op));
      return NULL;
  }

  LLVMValueRef lhs = emit_expr(codegen, binary->lhs);
  LLVMValueRef rhs = emit_expr(codegen, binary->rhs);
  if (binary->lhs->ty.ty == AST_TYPE_FLOAT) {
    return LLVMBuildFCmp(codegen->llvm_builder, fop, lhs, rhs, "cmp");
  }
  return LLVMBuildICmp(codegen->llvm_builder, iop, lhs, rhs, "cmp");
}

LLVMValueRef emit_binary_expr(struct codegen *codegen, struct ast_expr_binary *binary,
                              struct ast_ty *ty) {
  LLVMValueRef lhs = emit_expr(codegen, binary->lhs);
  LLVMValueRef rhs = emit_expr(codegen, binary->rhs);

  if (ty->ty == AST_TYPE_FVEC) {
    size_t element_count = ty->fvec.width;
    if (binary->lhs->ty.ty != binary->rhs->ty.ty) {
      // TODO: order of ops, find which one is the scalar broadcast vector
      LLVMTypeRef vecty = LLVMVectorType(LLVMFloatType(), element_count);
      LLVMValueRef zero = LLVMConstNull(vecty);
      LLVMValueRef undef = LLVMGetUndef(vecty);
      LLVMValueRef bvec = LLVMBuildInsertElement(codegen->llvm_builder, undef, rhs,
                                                 LLVMConstInt(LLVMInt32Type(), 0, 0), "broadcast");
      rhs = LLVMBuildShuffleVector(codegen->llvm_builder, bvec, undef, zero, "shuffle");
    }
  }

  if (ty->ty == AST_TYPE_FLOAT || ty->ty == AST_TYPE_FVEC) {
    switch (binary->op) {
      case AST_BINARY_OP_ADD:
        return LLVMBuildFAdd(codegen->llvm_builder, lhs, rhs, "fadd");
      case AST_BINARY_OP_SUB:
        return LLVMBuildFSub(codegen->llvm_builder, lhs, rhs, "fsub");
      case AST_BINARY_OP_MUL:
        return LLVMBuildFMul(codegen->llvm_builder, lhs, rhs, "fmul");
      case AST_BINARY_OP_DIV:
        return LLVMBuildFDiv(codegen->llvm_builder, lhs, rhs, "fdiv");
      case AST_BINARY_OP_MOD:
        return LLVMBuildFRem(codegen->llvm_builder, lhs, rhs, "fmod");
    }
  } else {
    switch (binary->op) {
      case AST_BINARY_OP_ADD:
        return LLVMBuildAdd(codegen->llvm_builder, lhs, rhs, "add");
      case AST_BINARY_OP_SUB:
        return LLVMBuildSub(codegen->llvm_builder, lhs, rhs, "sub");
      case AST_BINARY_OP_MUL:
        return LLVMBuildMul(codegen->llvm_builder, lhs, rhs, "mul");
      case AST_BINARY_OP_DIV:
        return LLVMBuildSDiv(codegen->llvm_builder, lhs, rhs, "div");
      case AST_BINARY_OP_MOD:
        return LLVMBuildSRem(codegen->llvm_builder, lhs, rhs, "mod");
      case AST_BINARY_OP_BITOR:
        return LLVMBuildOr(codegen->llvm_builder, lhs, rhs, "or");
      case AST_BINARY_OP_BITAND:
        return LLVMBuildAnd(codegen->llvm_builder, lhs, rhs, "and");
      case AST_BINARY_OP_BITXOR:
        return LLVMBuildXor(codegen->llvm_builder, lhs, rhs, "xor");
      case AST_BINARY_OP_LSHIFT:
        return LLVMBuildShl(codegen->llvm_builder, lhs, rhs, "shl");
      case AST_BINARY_OP_RSHIFT:
        return LLVMBuildAShr(codegen->llvm_builder, lhs, rhs, "shr");
    }
  }

  fprintf(stderr, "unhandled binary op %d\n", binary->op);
  return NULL;
}
