#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <llvm-c/DebugInfo.h>
#include <llvm-c/Target.h>
#include <llvm-c/Types.h>
#include <malloc.h>
#include <string.h>

#include "ast.h"
#include "codegen.h"
#include "compiler.h"
#include "internal.h"
#include "kv.h"
#include "scope.h"
#include "types.h"
#include "utility.h"

LLVMValueRef emit_lvalue(struct codegen *codegen, struct ast_expr *ast) {
  switch (ast->type) {
    case AST_EXPR_TYPE_VARIABLE: {
      struct scope_entry *lookup =
          scope_lookup(codegen->scope, ast->variable.ident.value.identv.ident, 1);

      return lookup->ref;
    } break;

    case AST_EXPR_TYPE_DEREF: {
      LLVMValueRef target = emit_expr(codegen, ast->deref.target);

      struct ast_ty *target_ty = &ast->deref.target->ty;
      if (target_ty->ty == AST_TYPE_POINTER) {
        target_ty = ptr_pointee_type(target_ty);
      }

      LLVMTypeRef expr_ty = ast_ty_to_llvm_ty(codegen, target_ty);

      if (target_ty->ty == AST_TYPE_MATRIX) {
        // matrix -> GEP the row
        LLVMValueRef indicies[2] = {
            LLVMConstInt(LLVMInt32TypeInContext(codegen->llvm_context), 0, 0),
            LLVMConstInt(LLVMInt32TypeInContext(codegen->llvm_context),
                         ast->deref.field_idx * ast->ty.matrix.rows, 0),
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
      if (target_ty->structty.is_union) {
        return target;
      }

      // struct -> GEP the field
      return LLVMBuildStructGEP2(codegen->llvm_builder, expr_ty, target,
                                 (unsigned int)ast->deref.field_idx, "deref.gep");
    }; break;

    case AST_EXPR_TYPE_ARRAY_INDEX: {
      struct scope_entry *entry =
          scope_lookup(codegen->scope, ast->array_index.ident.value.identv.ident, 1);

      LLVMValueRef index = emit_expr(codegen, ast->array_index.index);

      struct ast_ty *underlying = &entry->vdecl->ty;
      if (underlying->ty == AST_TYPE_POINTER) {
        // underlying = ptr_pointee_type(&entry->vdecl->ty);
      }

      LLVMTypeRef underlying_ty = ast_ty_to_llvm_ty(codegen, underlying);

      LLVMValueRef src = entry->ref;
      if (underlying->ty == AST_TYPE_POINTER) {
        return LLVMBuildGEP2(codegen->llvm_builder, underlying_ty, src, &index, 1, "ptr.index.gep");
      }

      LLVMValueRef gep[] = {LLVMConstInt(LLVMInt32TypeInContext(codegen->llvm_context), 0, 0),
                            index};
      return LLVMBuildGEP2(codegen->llvm_builder, underlying_ty, src, gep, 2, "");
    } break;

    default:
      compiler_log(codegen->compiler, LogLevelDebug, "codegen",
                   "emit_lvalue called on non-lvalue expr %d", ast->type);
  }

  return NULL;
}
