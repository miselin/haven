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
                         ast->expr.deref.field_idx * ast->ty->oneof.matrix.rows, 0),
        };

        return LLVMBuildGEP2(codegen->llvm_builder, expr_ty, target, indicies, 2,
                             "matrix.deref.row");
      }

      // vector -> extractelement
      if (target_ty->ty == AST_TYPE_FVEC) {
        // TODO: needs to become an InsertElement - requires logic in handling of
        // AST_EXPR_TYPE_ASSIGN

        compiler_log(codegen->compiler, LogLevelWarning, "codegen",
                     "fvec deref as lvalue not implemented yet");
        return NULL;
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

      LLVMTypeRef result_ty = ast_ty_to_llvm_ty(codegen, ast->ty);

      if (lhs_ty->ty == AST_TYPE_POINTER) {
        LLVMValueRef lhs = emit_expr(codegen, ast->expr.array_index.target);
        compiler_log(codegen->compiler, LogLevelDebug, "codegen", "array index on pointer type");
        return LLVMBuildGEP2(codegen->llvm_builder, result_ty, lhs, &index, 1, "ptr.index.lvalue");
      } else if (lhs_ty->ty == AST_TYPE_FVEC) {
        // TODO
      } else if (lhs_ty->ty == AST_TYPE_MATRIX) {
        // TODO
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
