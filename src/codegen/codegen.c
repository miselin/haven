
#include "codegen.h"

#include <llvm-c-18/llvm-c/Types.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <malloc.h>
#include <string.h>

#include "ast.h"
#include "internal.h"
#include "kv.h"
#include "scope.h"
#include "tokens.h"
#include "types.h"
#include "utility.h"

static void emit_ast(struct codegen *codegen, struct ast_program *ast);
static void emit_toplevel(struct codegen *codegen, struct ast_toplevel *ast);
static LLVMValueRef emit_stmt(struct codegen *codegen, struct ast_stmt *ast);

struct codegen *new_codegen(struct ast_program *ast) {
  struct codegen *result = calloc(1, sizeof(struct codegen));
  result->ast = ast;
  result->llvm_module = LLVMModuleCreateWithName("<stdin>");
  result->llvm_builder = LLVMCreateBuilder();

  // LLVMContextRef context = LLVMGetGlobalContext();
  // LLVMContextSetDiscardValueNames(context, 1);

  result->functions = new_kv();

  result->scope = enter_scope(NULL);
  return result;
}

int codegen_run(struct codegen *codegen) {
  emit_ast(codegen, codegen->ast);

  char *error = NULL;
  int rc = LLVMVerifyModule(codegen->llvm_module, LLVMReturnStatusAction, &error);
  if (rc) {
    fprintf(stderr, "verification failed: %s\n", error);
  }
  LLVMDisposeMessage(error);

  return 0;
}

char *codegen_ir(struct codegen *codegen) {
  return LLVMPrintModuleToString(codegen->llvm_module);
}

void codegen_dispose_ir(char *ir) {
  LLVMDisposeMessage(ir);
}

void destroy_codegen(struct codegen *codegen) {
  void *iter = scope_iter(codegen->scope);
  while (!scope_end(iter)) {
    struct scope_entry *entry = scope_next(&iter);
    if (entry->fdecl) {
      free(entry->param_types);
    }
  }

  destroy_kv(codegen->functions);
  exit_scope(codegen->scope);
  LLVMDisposeBuilder(codegen->llvm_builder);
  LLVMDisposeModule(codegen->llvm_module);
  free(codegen);
}

static void emit_ast(struct codegen *codegen, struct ast_program *ast) {
  struct ast_toplevel *decl = ast->decls;
  while (decl) {
    emit_toplevel(codegen, decl);
    decl = decl->next;
  }
}

static void emit_toplevel(struct codegen *codegen, struct ast_toplevel *ast) {
  if (ast->is_fn) {
    emit_fdecl(codegen, &ast->fdecl);
  } else {
    emit_vdecl(codegen, &ast->vdecl);
  }
}

LLVMValueRef emit_block(struct codegen *codegen, struct ast_block *ast) {
  codegen->scope = enter_scope(codegen->scope);
  struct ast_stmt *stmt = ast->stmt;
  LLVMValueRef last = NULL;
  while (stmt) {
    last = emit_stmt(codegen, stmt);
    stmt = stmt->next;
  }
  codegen->scope = exit_scope(codegen->scope);
  return last;
}

static LLVMValueRef emit_stmt(struct codegen *codegen, struct ast_stmt *ast) {
  switch (ast->type) {
    case AST_STMT_TYPE_EXPR: {
      LLVMValueRef expr = emit_expr(codegen, ast->expr);
      return expr;
    } break;

    case AST_STMT_TYPE_LET: {
      const char *ident = ast->let.ident.value.identv.ident;
      LLVMTypeRef var_type = ast_ty_to_llvm_ty(&ast->let.ty);
      LLVMValueRef var = new_alloca(codegen, var_type, ident);
      LLVMValueRef init = emit_expr(codegen, ast->let.init_expr);
      LLVMBuildStore(codegen->llvm_builder, init, var);

      struct scope_entry *entry = calloc(1, sizeof(struct scope_entry));
      entry->vdecl = &ast->let;
      entry->variable_type = var_type;
      entry->ref = var;
      scope_insert(codegen->scope, ident, entry);
      return var;
    } break;

    case AST_STMT_TYPE_ITER: {
      const char *ident = ast->iter.index_vdecl->ident.value.identv.ident;
      LLVMValueRef start = emit_expr(codegen, ast->iter.range.start);
      LLVMValueRef end = emit_expr(codegen, ast->iter.range.end);
      LLVMValueRef step = ast->iter.range.step ? emit_expr(codegen, ast->iter.range.step)
                                               : LLVMConstInt(LLVMInt64Type(), 1, 0);

      LLVMTypeRef var_type = ast_ty_to_llvm_ty(&ast->iter.index_vdecl->ty);

      codegen->scope = enter_scope(codegen->scope);

      LLVMValueRef index = new_alloca(codegen, var_type, ident);
      LLVMBuildStore(codegen->llvm_builder, start, index);

      // insert the index variable to scope
      struct scope_entry *entry = calloc(1, sizeof(struct scope_entry));
      entry->vdecl = ast->iter.index_vdecl;
      entry->variable_type = var_type;
      entry->ref = index;
      scope_insert(codegen->scope, ident, entry);

      // now build the actual loop

      LLVMBasicBlockRef cond_block = LLVMAppendBasicBlock(codegen->current_function, "loop.cond");
      LLVMBasicBlockRef body_block = LLVMAppendBasicBlock(codegen->current_function, "loop.body");
      LLVMBasicBlockRef step_block = LLVMAppendBasicBlock(codegen->current_function, "loop.step");
      LLVMBasicBlockRef end_block = LLVMAppendBasicBlock(codegen->current_function, "loop.end");

      LLVMBuildBr(codegen->llvm_builder, cond_block);

      LLVMPositionBuilderAtEnd(codegen->llvm_builder, cond_block);
      LLVMValueRef index_val = LLVMBuildLoad2(codegen->llvm_builder, var_type, index, "index");
      LLVMValueRef cmp = LLVMBuildICmp(codegen->llvm_builder, LLVMIntNE, index_val, end, "cmp");
      LLVMBuildCondBr(codegen->llvm_builder, cmp, body_block, end_block);

      LLVMPositionBuilderAtEnd(codegen->llvm_builder, body_block);
      emit_block(codegen, &ast->iter.block);

      LLVMBuildBr(codegen->llvm_builder, step_block);

      LLVMPositionBuilderAtEnd(codegen->llvm_builder, step_block);
      LLVMValueRef next = LLVMBuildAdd(codegen->llvm_builder, index_val, step, "next");
      LLVMBuildStore(codegen->llvm_builder, next, index);
      LLVMBuildBr(codegen->llvm_builder, cond_block);

      LLVMPositionBuilderAtEnd(codegen->llvm_builder, end_block);

      codegen->scope = exit_scope(codegen->scope);
    } break;

    case AST_STMT_TYPE_RETURN: {
      LLVMValueRef ret = emit_expr(codegen, ast->expr);
      LLVMBuildRet(codegen->llvm_builder, ret);
    } break;

    case AST_STMT_TYPE_STORE: {
      LLVMValueRef lhs = emit_expr(codegen, ast->store.lhs);
      LLVMValueRef rhs = emit_expr(codegen, ast->store.rhs);
      LLVMBuildStore(codegen->llvm_builder, rhs, lhs);
    } break;

    default:
      fprintf(stderr, "unhandled statement type %d\n", ast->type);
  }

  return NULL;
}
