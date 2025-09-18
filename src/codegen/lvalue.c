#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <llvm-c/DebugInfo.h>
#include <llvm-c/Target.h>
#include <llvm-c/Types.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "codegen.h"
#include "compiler.h"
#include "internal.h"
#include "scope.h"
#include "types.h"

LLVMValueRef emit_lvalue(struct codegen *codegen, struct ast_expr *ast) {
  switch (ast->type) {
    case AST_EXPR_TYPE_VARIABLE: {
      struct scope_entry *lookup =
          scope_lookup(codegen->scope, ast->expr.variable.ident.value.identv.ident, 1);

      LLVMTypeRef ty = LLVMTypeOf(lookup->ref);
      LLVMTypeKind kind = LLVMGetTypeKind(ty);
      if (kind != LLVMPointerTypeKind) {
        // We need to spill this value to create an lvalue, it's something like a param or other
        // bare value, not a ptr
        LLVMValueRef spilled =
            new_alloca(codegen, ast_ty_to_llvm_ty(codegen, ast->ty), "var.spill");
        LLVMBuildStore(codegen->llvm_builder, lookup->ref, spilled);
        return spilled;
      }

      return lookup->ref;
    } break;

    case AST_EXPR_TYPE_DEREF: {
      compiler_log(codegen->compiler, LogLevelDebug, "codegen", "emitting a deref lvalue");

      char name[512];

      LLVMValueRef target = emit_expr(codegen, ast->expr.deref.target);

      struct ast_ty *target_ty = ast->expr.deref.target->ty;
      struct ast_ty *orig_ty = target_ty;
      if (target_ty->ty == AST_TYPE_POINTER) {
        target_ty = ptr_pointee_type(target_ty);
      } else if (target_ty->ty == AST_TYPE_BOX) {
        target_ty = box_pointee_type(target_ty);
      }

      if (orig_ty->ty == AST_TYPE_BOX) {
        // needs to be the equivalent of an unbox without the subsequent load
        LLVMTypeRef box_type = codegen_box_type(codegen, orig_ty);
        target = LLVMBuildStructGEP2(codegen->llvm_builder, box_type, target, 1, "box.value");
      }

      LLVMTypeRef expr_ty = ast_ty_to_llvm_ty(codegen, target_ty);

      if (target_ty->ty == AST_TYPE_MATRIX) {
        // matrix -> GEP the row
        LLVMValueRef indicies[2] = {
            LLVMConstInt(LLVMInt32TypeInContext(codegen->llvm_context), 0, 0),
            LLVMConstInt(LLVMInt32TypeInContext(codegen->llvm_context),
                         ast->expr.deref.field_idx * ast->ty->oneof.matrix.cols, 0),
        };

        return LLVMBuildGEP2(codegen->llvm_builder, expr_ty, target, indicies, 2,
                             "matrix.deref.row");
      }

      // vector -> extractelement
      if (target_ty->ty == AST_TYPE_FVEC) {
        // Need to spill the fvec to be able to consider it an lvalue
        LLVMValueRef spilled = new_alloca(codegen, expr_ty, "fvec.spill");
        LLVMBuildStore(codegen->llvm_builder, target, spilled);
        return spilled;
      }

      // union -> read from first field, direct pointer access
      if (target_ty->ty == AST_TYPE_STRUCT && target_ty->oneof.structty.is_union) {
        return target;
      }

      compiler_log(codegen->compiler, LogLevelDebug, "codegen", "struct GEP2 field %s",
                   ast->expr.deref.field.value.identv.ident);

      // struct -> GEP the field
      snprintf(name, 512, "lv.deref.gep.%s", ast->expr.deref.field.value.identv.ident);
      return LLVMBuildStructGEP2(codegen->llvm_builder, expr_ty, target,
                                 (unsigned int)ast->expr.deref.field_idx, name);
    }; break;

    case AST_EXPR_TYPE_ARRAY_INDEX: {
      struct ast_ty *lhs_ty = ast->expr.array_index.target->ty;
      LLVMValueRef index = emit_expr(codegen, ast->expr.array_index.index);

      LLVMTypeRef lhs_llvm_ty = ast_ty_to_llvm_ty(codegen, lhs_ty);
      LLVMTypeRef result_ty = ast_underlying_ty_to_llvm_ty(codegen, ast->ty);

      if (lhs_ty->ty == AST_TYPE_POINTER) {
        LLVMValueRef lhs = emit_expr(codegen, ast->expr.array_index.target);
        compiler_log(codegen->compiler, LogLevelDebug, "codegen", "array index on pointer type");
        return LLVMBuildGEP2(codegen->llvm_builder, result_ty, lhs, &index, 1, "ptr.index.lvalue");
      } else if (lhs_ty->ty == AST_TYPE_FVEC) {
        LLVMValueRef lhs = emit_lvalue(codegen, ast->expr.array_index.target);

        // matrix -> GEP the row
        LLVMValueRef indicies[2] = {
            LLVMConstInt(LLVMInt32TypeInContext(codegen->llvm_context), 0, 0),
            index,
        };

        return LLVMBuildGEP2(codegen->llvm_builder, lhs_llvm_ty, lhs, indicies, 2, "fvec.deref");
      } else if (lhs_ty->ty == AST_TYPE_MATRIX) {
        LLVMValueRef lhs = emit_expr(codegen, ast->expr.array_index.target);

        LLVMTypeRef out_ty = LLVMVectorType(LLVMFloatTypeInContext(codegen->llvm_context),
                                            (unsigned int)lhs_ty->oneof.matrix.cols);
        LLVMValueRef out_alloca = LLVMBuildAlloca(codegen->llvm_builder, out_ty, "mat.row");
        LLVMValueRef out_raw = LLVMGetUndef(out_ty);

        // rows * cols = first element of row
        LLVMValueRef base = LLVMBuildMul(codegen->llvm_builder,
                                         const_i32(codegen, (int32_t)lhs_ty->oneof.matrix.cols),
                                         index, "matrix.index.row.base");

        LLVMValueRef *indices =
            (LLVMValueRef *)malloc(sizeof(LLVMValueRef) * lhs_ty->oneof.matrix.cols);
        for (size_t i = 0; i < lhs_ty->oneof.matrix.cols; i++) {
          indices[i] = LLVMBuildAdd(codegen->llvm_builder, base, const_i32(codegen, (int32_t)i),
                                    "matrix.index.col");
        }

        LLVMValueRef *elements =
            (LLVMValueRef *)malloc(sizeof(LLVMValueRef) * lhs_ty->oneof.matrix.cols);

        // extraction pass
        for (size_t i = 0; i < lhs_ty->oneof.matrix.cols; i++) {
          elements[i] = LLVMBuildExtractElement(codegen->llvm_builder, lhs, indices[i],
                                                "matrix.index.element");
        }

        // insertion pass
        for (size_t i = 0; i < lhs_ty->oneof.matrix.cols; i++) {
          out_raw = LLVMBuildInsertElement(codegen->llvm_builder, out_raw, elements[i],
                                           const_i32(codegen, (int32_t)i), "mat.row.insert");
        }

        LLVMBuildStore(codegen->llvm_builder, out_raw, out_alloca);

        free(indices);
        free(elements);

        return out_alloca;
        /*

        // index * cols = row offset
        // LLVMValueRef mul = LLVMBuildMul(
        //     codegen->llvm_builder,
        //     const_i32(codegen, (int32_t)ast->expr.array_index.target->ty->oneof.matrix.cols),
        //     index, "matrix.index.row");

        LLVMValueRef indicies[2] = {
            LLVMConstInt(LLVMInt32TypeInContext(codegen->llvm_context), 0, 0),
            index,
        };

        // recast the type into [ cols x < rows x float> ] so GEP extracts the row
        LLVMTypeRef extract_ty = LLVMArrayType(
            LLVMVectorType(LLVMFloatTypeInContext(codegen->llvm_context),
                           (unsigned int)ast->expr.array_index.target->ty->oneof.matrix.cols),
            (unsigned int)ast->expr.array_index.target->ty->oneof.matrix.rows);

        return LLVMBuildGEP2(codegen->llvm_builder, extract_ty, lhs, indicies, 2,
                             "matrix.index.gep");
                             */
      } else if (lhs_ty->ty == AST_TYPE_ARRAY) {
        compiler_log(codegen->compiler, LogLevelDebug, "codegen", "array index on array");

        LLVMValueRef lhs = emit_lvalue(codegen, ast->expr.array_index.target);
        return LLVMBuildGEP2(codegen->llvm_builder, result_ty, lhs, &index, 1,
                             "array.index.lvalue");
      } else {
        compiler_log(codegen->compiler, LogLevelDebug, "codegen",
                     "array index unimplemented for type %d", lhs_ty->ty);
      }
    } break;

    case AST_EXPR_TYPE_CAST: {
      LLVMValueRef expr = emit_lvalue(codegen, ast->expr.cast.expr);
      LLVMValueRef result = emit_cast(codegen, expr, ast->expr.cast.expr->ty, ast->ty);
      return result;
    } break;

    default:
      compiler_log(codegen->compiler, LogLevelDebug, "codegen",
                   "emit_lvalue called on non-lvalue expr %d", ast->type);
  }

  return NULL;
}
