
#include <inttypes.h>
#include <libgen.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <llvm-c/DebugInfo.h>
#include <llvm-c/Support.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Transforms/PassBuilder.h>
#include <llvm-c/Types.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "codegen.h"
#include "compiler.h"
#include "internal.h"
#include "scope.h"
#include "types.h"

LLVMValueRef emit_stmt(struct codegen *codegen, struct ast_stmt *ast, LLVMValueRef into) {
  compiler_log(codegen->compiler, LogLevelDebug, "codegen", "emit stmt %d", ast->type);
  update_debug_loc(codegen, &ast->loc);
  switch (ast->type) {
    case AST_STMT_TYPE_EXPR: {
      return emit_expr_into(codegen, ast->stmt.expr, into);
    } break;

    case AST_STMT_TYPE_LET: {
      const char *ident = ast->stmt.let.ident.value.identv.ident;
      LLVMTypeRef var_type = ast_underlying_ty_to_llvm_ty(codegen, ast->stmt.let.ty);
      LLVMValueRef var = new_alloca(codegen, var_type, ident);
      LLVMValueRef init = emit_expr_into(codegen, ast->stmt.let.init_expr, var);
      if (init != var) {
        emit_store(codegen, ast->stmt.let.init_expr->ty, init, var);
      }

      struct scope_entry *entry = calloc(1, sizeof(struct scope_entry));
      entry->flags = ast->stmt.let.flags;
      entry->variable_type = var_type;
      entry->ref = var;
      scope_insert(codegen->scope, ident, entry);
      return var;
    } break;

    case AST_STMT_TYPE_ITER: {
      const char *ident = ast->stmt.iter.index.ident.value.identv.ident;
      LLVMValueRef start = emit_expr(codegen, ast->stmt.iter.range.start);
      LLVMValueRef end = emit_expr(codegen, ast->stmt.iter.range.end);
      LLVMValueRef step = ast->stmt.iter.range.step
                              ? emit_expr(codegen, ast->stmt.iter.range.step)
                              : LLVMConstInt(LLVMInt64TypeInContext(codegen->llvm_context), 1, 0);

      int direction = 1;
      int64_t iter_step = 1;
      if (ast->stmt.iter.range.step) {
        if (extract_constant_int(ast->stmt.iter.range.step, &iter_step) == 0) {
          if (iter_step < 0) {
            direction = -1;
          }
        }
      }

      LLVMTypeRef var_type = ast_ty_to_llvm_ty(codegen, ast->stmt.iter.index_ty);

      codegen_internal_enter_scope(codegen, &ast->loc, 1);

      LLVMValueRef index = new_alloca(codegen, var_type, ident);

      emit_store(codegen, ast->stmt.iter.range.start->ty, start, index);

      // insert the index variable to scope
      struct scope_entry *entry = calloc(1, sizeof(struct scope_entry));
      // entry->vdecl = ast->stmt.iter.index_vdecl;
      entry->variable_type = var_type;
      entry->ref = index;
      scope_insert(codegen->scope, ident, entry);

      // now build the actual loop

      LLVMBasicBlockRef cond_block = LLVMAppendBasicBlockInContext(
          codegen->llvm_context, codegen->current_function, "iter.cond");
      LLVMBasicBlockRef body_block = LLVMAppendBasicBlockInContext(
          codegen->llvm_context, codegen->current_function, "iter.body");
      LLVMBasicBlockRef step_block = LLVMAppendBasicBlockInContext(
          codegen->llvm_context, codegen->current_function, "iter.step");
      LLVMBasicBlockRef end_block = LLVMAppendBasicBlockInContext(
          codegen->llvm_context, codegen->current_function, "iter.end");

      LLVMBuildBr(codegen->llvm_builder, cond_block);

      LLVMPositionBuilderAtEnd(codegen->llvm_builder, cond_block);
      LLVMValueRef index_val = LLVMBuildLoad2(codegen->llvm_builder, var_type, index, "index");
      LLVMValueRef cmp = LLVMBuildICmp(
          codegen->llvm_builder, direction > 0 ? LLVMIntSLE : LLVMIntSGE, index_val, end, "cmp");
      LLVMValueRef loop_cond_br =
          LLVMBuildCondBr(codegen->llvm_builder, cmp, body_block, end_block);

      LLVMPositionBuilderAtEnd(codegen->llvm_builder, body_block);
      emit_block(codegen, &ast->stmt.iter.block);

      LLVMBuildBr(codegen->llvm_builder, step_block);

      LLVMPositionBuilderAtEnd(codegen->llvm_builder, step_block);
      LLVMValueRef next = LLVMBuildAdd(codegen->llvm_builder, index_val, step, "iter.next");
      emit_store(codegen, ast->stmt.iter.index_ty, next, index);
      LLVMBuildBr(codegen->llvm_builder, cond_block);

      LLVMPositionBuilderAtEnd(codegen->llvm_builder, end_block);

      int64_t istart = 0;
      int64_t iend = 0;
      int rcstart = extract_constant_int(ast->stmt.iter.range.start, &istart);
      int rcend = extract_constant_int(ast->stmt.iter.range.end, &iend);

      // ok llvm is going a little far on unrolling some loops... let's not do this
      if (0 && rcstart == 0 && rcend == 0) {
        // range is constant, we can annotate an unroll count
        int64_t iters = ((iend - istart) / iter_step) + 1;

        compiler_log(codegen->compiler, LogLevelDebug, "codegen", "loop has %" PRIi64 " iterations",
                     iters);

        LLVMMetadataRef count_md = LLVMValueAsMetadata(
            LLVMConstInt(LLVMInt64TypeInContext(codegen->llvm_context), (uint64_t)iters, 1));

        LLVMMetadataRef unroll_enable = LLVMMDStringInContext2(
            codegen->llvm_context, "llvm.loop.unroll.enable", strlen("llvm.loop.unroll.enable"));
        LLVMMetadataRef unroll_count_str = LLVMMDStringInContext2(
            codegen->llvm_context, "llvm.loop.unroll.count", strlen("llvm.loop.unroll.count"));

        LLVMMetadataRef unroll_count[2] = {unroll_count_str, count_md};

        LLVMMetadataRef unroll_enable_node =
            LLVMMDNodeInContext2(codegen->llvm_context, &unroll_enable, 1);
        LLVMMetadataRef unroll_count_node =
            LLVMMDNodeInContext2(codegen->llvm_context, unroll_count, 2);

        LLVMMetadataRef mds[] = {unroll_enable_node, unroll_count_node};

        LLVMMetadataRef loop_node = LLVMMDNodeInContext2(codegen->llvm_context, mds, 2);

        LLVMValueRef md_value = LLVMMetadataAsValue(codegen->llvm_context, loop_node);
        LLVMSetMetadata(loop_cond_br,
                        LLVMGetMDKindIDInContext(codegen->llvm_context, "llvm.loop", 9), md_value);
      }

      codegen_internal_leave_scope(codegen, 1);
    } break;

    case AST_STMT_TYPE_RETURN: {
      if (ast->stmt.expr) {
        LLVMValueRef ret = emit_expr(codegen, ast->stmt.expr);

        if (ast->stmt.expr->ty->ty == AST_TYPE_BOX) {
          // ref the box on the way out
          compiler_log(codegen->compiler, LogLevelDebug, "codegen",
                       "box ref due to STMT_TYPE_RETURN");
          codegen_box_ref(codegen, ret, 0);
        }

        emit_store(codegen, ast->stmt.expr->ty, ret, codegen->retval);
      }
      LLVMBuildBr(codegen->llvm_builder, codegen->return_block);
      LLVMBasicBlockRef new_block = LLVMAppendBasicBlockInContext(
          codegen->llvm_context, codegen->current_function, "after.return");
      LLVMPositionBuilderAtEnd(codegen->llvm_builder, new_block);
    } break;

    case AST_STMT_TYPE_STORE: {
      LLVMValueRef lhs = emit_expr(codegen, ast->stmt.store.lhs);
      // Need to store into the value of the box, not the var itself
      if (ast->stmt.store.lhs->ty->ty == AST_TYPE_BOX) {
        LLVMTypeRef box_type = codegen_box_type(codegen, ast->stmt.store.lhs->ty);
        lhs = LLVMBuildStructGEP2(codegen->llvm_builder, box_type, lhs, 1, "box.value");
      }
      LLVMValueRef rhs = emit_expr(codegen, ast->stmt.store.rhs);
      emit_store(codegen, ast->stmt.store.rhs->ty, rhs, lhs);
    } break;

    case AST_STMT_TYPE_DEFER: {
      struct defer_entry *entry = calloc(1, sizeof(struct defer_entry));

      // defers will be reordered after the function is emitted - but emit now to ensure the
      // variable references are maintained
      LLVMBasicBlockRef current = LLVMGetInsertBlock(codegen->llvm_builder);
      entry->llvm_block =
          LLVMAppendBasicBlockInContext(codegen->llvm_context, codegen->current_function, "defer");
      LLVMPositionBuilderAtEnd(codegen->llvm_builder, entry->llvm_block);
      emit_expr(codegen, ast->stmt.expr);
      entry->llvm_block_after = LLVMGetInsertBlock(codegen->llvm_builder);
      LLVMPositionBuilderAtEnd(codegen->llvm_builder, current);

      entry->next = codegen->defer_head;
      codegen->defer_head = entry;
    } break;

    case AST_STMT_TYPE_WHILE: {
      LLVMBasicBlockRef cond_block = LLVMAppendBasicBlockInContext(
          codegen->llvm_context, codegen->current_function, "while.cond");
      LLVMBasicBlockRef body_block = LLVMAppendBasicBlockInContext(
          codegen->llvm_context, codegen->current_function, "while.body");
      LLVMBasicBlockRef end_block = LLVMAppendBasicBlockInContext(
          codegen->llvm_context, codegen->current_function, "while.end");

      struct scope_entry *loop_break = calloc(1, sizeof(struct scope_entry));
      loop_break->block = end_block;

      struct scope_entry *loop_continue = calloc(1, sizeof(struct scope_entry));
      loop_continue->block = cond_block;

      codegen_internal_enter_scope(codegen, &ast->loc, 1);
      scope_insert(codegen->scope, "@loop.break", loop_break);
      scope_insert(codegen->scope, "@loop.continue", loop_continue);

      LLVMBuildBr(codegen->llvm_builder, cond_block);
      LLVMPositionBuilderAtEnd(codegen->llvm_builder, cond_block);

      LLVMTypeRef cond_type = ast_ty_to_llvm_ty(codegen, ast->stmt.while_stmt.cond->ty);

      LLVMValueRef cond = emit_expr(codegen, ast->stmt.while_stmt.cond);
      LLVMValueRef comp = LLVMBuildICmp(codegen->llvm_builder, LLVMIntNE, cond,
                                        LLVMConstInt(cond_type, 0, 0), "while.cond");
      LLVMBuildCondBr(codegen->llvm_builder, comp, body_block, end_block);

      LLVMPositionBuilderAtEnd(codegen->llvm_builder, body_block);
      emit_block(codegen, &ast->stmt.while_stmt.block);
      LLVMBuildBr(codegen->llvm_builder, cond_block);

      LLVMPositionBuilderAtEnd(codegen->llvm_builder, end_block);
      codegen_internal_leave_scope(codegen, 1);
    } break;

    case AST_STMT_TYPE_BREAK: {
      struct scope_entry *entry = scope_lookup(codegen->scope, "@loop.break", 1);
      LLVMBuildBr(codegen->llvm_builder, entry->block);
    } break;

    case AST_STMT_TYPE_CONTINUE: {
      struct scope_entry *entry = scope_lookup(codegen->scope, "@loop.continue", 1);
      LLVMBuildBr(codegen->llvm_builder, entry->block);
    } break;

    default:
      fprintf(stderr, "unhandled statement type %d\n", ast->type);
  }

  return NULL;
}
