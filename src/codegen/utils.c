#include <llvm-c-18/llvm-c/Types.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>

#include "ast.h"
#include "codegen.h"
#include "internal.h"
#include "kv.h"
#include "types.h"
#include "utility.h"

LLVMValueRef new_alloca(struct codegen *codegen, LLVMTypeRef type, const char *name) {
  LLVMBasicBlockRef current_block = LLVMGetInsertBlock(codegen->llvm_builder);

  // insert allocas at the start of the entry block
  if (codegen->last_alloca) {
    LLVMValueRef next = LLVMGetNextInstruction(codegen->last_alloca);
    if (next) {
      LLVMPositionBuilderBefore(codegen->llvm_builder, next);
    } else {
      LLVMPositionBuilderAtEnd(codegen->llvm_builder, codegen->entry_block);
    }
  } else {
    LLVMPositionBuilderAtEnd(codegen->llvm_builder, codegen->entry_block);
  }

  LLVMValueRef var = LLVMBuildAlloca(codegen->llvm_builder, type, name);
  codegen->last_alloca = var;

  LLVMPositionBuilderAtEnd(codegen->llvm_builder, current_block);

  return var;
}

LLVMValueRef cast(struct codegen *codegen, LLVMValueRef value, struct ast_ty *from,
                  struct ast_ty *to) {
  if (same_type(from, to)) {
    return value;
  }

  LLVMTypeRef dest_ty = ast_ty_to_llvm_ty(codegen, to);

  if (!same_type_class(from, to)) {
    if (from->ty == AST_TYPE_INTEGER) {
      return LLVMBuildSIToFP(codegen->llvm_builder, value, dest_ty, "fpconv");
    } else {
      return LLVMBuildFPToSI(codegen->llvm_builder, value, dest_ty, "sconv");
    }
  }

  if (narrower_type(from, to)) {
    // TODO: sext if signed integer
    return LLVMBuildZExtOrBitCast(codegen->llvm_builder, value, dest_ty, "widening");
  } else {
    return LLVMBuildTruncOrBitCast(codegen->llvm_builder, value, dest_ty, "narrowing");
  }
}

LLVMTypeRef ast_ty_to_llvm_ty(struct codegen *codegen, struct ast_ty *ty) {
  LLVMTypeRef inner = NULL;

  switch (ty->ty) {
    case AST_TYPE_INTEGER:
      switch (ty->integer.width) {
        case 1:
          inner = LLVMInt1Type();
          break;
        case 8:
          inner = LLVMInt8Type();
          break;
        case 16:
          inner = LLVMInt16Type();
          break;
        case 32:
          inner = LLVMInt32Type();
          break;
        case 64:
          inner = LLVMInt64Type();
          break;
        default:
          inner = LLVMIntType((unsigned int)ty->integer.width);
      }
      break;
    case AST_TYPE_CHAR:
      inner = LLVMInt8Type();
      break;
    case AST_TYPE_STRING:
      inner = LLVMPointerType(LLVMInt8Type(), 0);
      break;
    case AST_TYPE_FLOAT:
      inner = LLVMFloatType();
      break;
    case AST_TYPE_FVEC:
      inner = LLVMVectorType(LLVMFloatType(), (unsigned int)ty->fvec.width);
      break;
    case AST_TYPE_VOID:
      inner = LLVMVoidType();
      break;
    case AST_TYPE_ARRAY:
      inner = LLVMArrayType(ast_ty_to_llvm_ty(codegen, ty->array.element_ty),
                            (unsigned int)ty->array.width);
      break;
    case AST_TYPE_STRUCT: {
      struct struct_entry *entry = kv_lookup(codegen->structs, ty->name);
      if (!entry) {
        fprintf(stderr, "struct %s not found in codegen\n", ty->name);
        return NULL;
      }
      return entry->type;
    } break;
    default:
      fprintf(stderr, "unhandled type %d in conversion to LLVM TypeRef\n", ty->ty);
      return NULL;
  }

  if (ty->flags & TYPE_FLAG_PTR) {
    return LLVMPointerType(inner, 0);
  }

  return inner;
}
