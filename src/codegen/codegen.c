
#include "codegen.h"

#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <llvm-c/DebugInfo.h>
#include <llvm-c/Types.h>
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
  result->llvm_module = LLVMModuleCreateWithName(ast->loc.file);
  result->llvm_builder = LLVMCreateBuilder();
  result->llvm_dibuilder = LLVMCreateDIBuilder(result->llvm_module);

  LLVMContextRef context = LLVMGetGlobalContext();
  LLVMContextSetDiscardValueNames(context, 0);

  result->functions = new_kv();

  result->structs = new_kv();

  // TODO: split file into dir + file
  result->file_metadata = LLVMDIBuilderCreateFile(result->llvm_dibuilder, ast->loc.file,
                                                  strlen(ast->loc.file), "examples", 8);
  result->compile_unit = LLVMDIBuilderCreateCompileUnit(
      result->llvm_dibuilder, LLVMDWARFSourceLanguageC, result->file_metadata, "mattc", 5, 0, "", 0,
      0, "", 0, LLVMDWARFEmissionFull, 0, 0, 0, "", 0, "", 0);

  codegen_internal_enter_scope(result, &ast->loc, 0);

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
  void *iter = kv_iter(codegen->structs);
  while (!kv_end(iter)) {
    struct struct_entry *entry = kv_next(&iter);
    free(entry);
  }
  destroy_kv(codegen->structs);

  iter = scope_iter(codegen->scope);
  while (!scope_end(iter)) {
    struct scope_entry *entry = scope_next(&iter);
    if (entry->fdecl) {
      free(entry->param_types);
    }
  }

  destroy_kv(codegen->functions);
  codegen_internal_leave_scope(codegen, 0);
  LLVMDisposeDIBuilder(codegen->llvm_dibuilder);
  LLVMDisposeBuilder(codegen->llvm_builder);
  LLVMDisposeModule(codegen->llvm_module);
  free(codegen);
}

static void emit_ast(struct codegen *codegen, struct ast_program *ast) {
  update_debug_loc(codegen, &ast->loc);
  struct ast_toplevel *decl = ast->decls;
  while (decl) {
    emit_toplevel(codegen, decl);
    decl = decl->next;
  }
}

static LLVMTypeRef emit_struct_type(struct codegen *codegen, struct ast_ty *ty) {
  char buf[256 + 8];
  snprintf(buf, 256 + 8, "struct.%s", ty->name);

  struct struct_entry *entry = calloc(1, sizeof(struct struct_entry));
  kv_insert(codegen->structs, ty->name, entry);
  entry->type = LLVMStructCreateNamed(LLVMGetGlobalContext(), buf);

  LLVMTypeRef *element_types = malloc(sizeof(LLVMTypeRef) * ty->structty.num_fields);
  struct ast_struct_field *field = ty->structty.fields;
  for (size_t i = 0; i < ty->structty.num_fields; i++) {
    element_types[i] = ast_ty_to_llvm_ty(codegen, field->ty);
    field = field->next;
  }

  LLVMStructSetBody(entry->type, element_types, (unsigned int)ty->structty.num_fields, 0);

  // LLVMAddGlobal(codegen->llvm_module, entry->type, ty->name);

  free(element_types);
  return entry->type;
}

static LLVMTypeRef emit_enum_type(struct codegen *codegen, struct ast_ty *ty) {
  char buf[256 + 8];
  snprintf(buf, 256 + 8, "enum.%s", ty->name);

  struct struct_entry *entry = calloc(1, sizeof(struct struct_entry));
  kv_insert(codegen->structs, ty->name, entry);

  if (ty->enumty.no_wrapped_fields) {
    // simple integer enum
    entry->type = LLVMInt32Type();
    return entry->type;
  }

  entry->type = LLVMStructCreateNamed(LLVMGetGlobalContext(), buf);

  size_t total_size = 0;
  struct ast_enum_field *field = ty->enumty.fields;
  while (field) {
    if (field->has_inner) {
      size_t sz = type_size(&field->inner);
      if (sz > total_size) {
        total_size = sz;
      }
    }
    total_size += field->has_inner ? 2 : 1;
    field = field->next;
  }

  LLVMTypeRef fields[2] = {
      LLVMInt32Type(),  // identifying tag
      LLVMArrayType(LLVMInt8Type(),
                    (unsigned int)total_size),  // data storage for largest possible field
  };

  LLVMStructSetBody(entry->type, fields, 2, 0);

  return entry->type;
}

static void emit_toplevel(struct codegen *codegen, struct ast_toplevel *ast) {
  update_debug_loc(codegen, &ast->loc);
  if (ast->type == AST_DECL_TYPE_FDECL) {
    emit_fdecl(codegen, &ast->fdecl, &ast->loc);
  } else if (ast->type == AST_DECL_TYPE_VDECL) {
    emit_vdecl(codegen, &ast->vdecl);
  } else if (ast->type == AST_DECL_TYPE_TYDECL) {
    if (ast->tydecl.ty.ty == AST_TYPE_STRUCT) {
      // generate the struct type
      emit_struct_type(codegen, &ast->tydecl.ty);
    } else if (ast->tydecl.ty.ty == AST_TYPE_ENUM) {
      emit_enum_type(codegen, &ast->tydecl.ty);
    } else {
      fprintf(stderr, "unhandled top level type declaration type %d\n", ast->tydecl.ty.ty);
    }
  } else if (ast->type == AST_DECL_TYPE_PREPROC) {
    // no-op in codegen
  } else {
    fprintf(stderr, "unhandled toplevel type %d\n", ast->type);
  }
}

LLVMValueRef emit_block(struct codegen *codegen, struct ast_block *ast) {
  codegen_internal_enter_scope(codegen, &ast->loc, 1);
  update_debug_loc(codegen, &ast->loc);
  struct ast_stmt *stmt = ast->stmt;
  LLVMValueRef last = NULL;
  while (stmt) {
    last = emit_stmt(codegen, stmt);
    stmt = stmt->next;
  }
  codegen_internal_leave_scope(codegen, 1);
  return last;
}

static LLVMValueRef emit_stmt(struct codegen *codegen, struct ast_stmt *ast) {
  update_debug_loc(codegen, &ast->loc);
  switch (ast->type) {
    case AST_STMT_TYPE_EXPR: {
      LLVMValueRef expr = emit_expr(codegen, ast->expr);
      return expr;
    } break;

    case AST_STMT_TYPE_LET: {
      const char *ident = ast->let.ident.value.identv.ident;
      LLVMTypeRef var_type = ast_ty_to_llvm_ty(codegen, &ast->let.ty);
      LLVMValueRef var = new_alloca(codegen, var_type, ident);
      LLVMValueRef init = emit_expr_into(codegen, ast->let.init_expr, var);
      if (init != var) {
        emit_store(codegen, &ast->let.init_expr->ty, init, var);
      }

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

      LLVMTypeRef var_type = ast_ty_to_llvm_ty(codegen, &ast->iter.index_vdecl->ty);

      codegen_internal_enter_scope(codegen, &ast->loc, 1);

      LLVMValueRef index = new_alloca(codegen, var_type, ident);
      LLVMBuildStore(codegen->llvm_builder, start, index);

      // insert the index variable to scope
      struct scope_entry *entry = calloc(1, sizeof(struct scope_entry));
      entry->vdecl = ast->iter.index_vdecl;
      entry->variable_type = var_type;
      entry->ref = index;
      scope_insert(codegen->scope, ident, entry);

      // now build the actual loop

      LLVMBasicBlockRef cond_block = LLVMAppendBasicBlock(codegen->current_function, "iter.cond");
      LLVMBasicBlockRef body_block = LLVMAppendBasicBlock(codegen->current_function, "iter.body");
      LLVMBasicBlockRef step_block = LLVMAppendBasicBlock(codegen->current_function, "iter.step");
      LLVMBasicBlockRef end_block = LLVMAppendBasicBlock(codegen->current_function, "iter.end");

      LLVMBuildBr(codegen->llvm_builder, cond_block);

      LLVMPositionBuilderAtEnd(codegen->llvm_builder, cond_block);
      LLVMValueRef index_val = LLVMBuildLoad2(codegen->llvm_builder, var_type, index, "index");
      LLVMValueRef cmp = LLVMBuildICmp(codegen->llvm_builder, LLVMIntNE, index_val, end, "cmp");
      LLVMBuildCondBr(codegen->llvm_builder, cmp, body_block, end_block);

      LLVMPositionBuilderAtEnd(codegen->llvm_builder, body_block);
      emit_block(codegen, &ast->iter.block);

      LLVMBuildBr(codegen->llvm_builder, step_block);

      LLVMPositionBuilderAtEnd(codegen->llvm_builder, step_block);
      LLVMValueRef next = LLVMBuildAdd(codegen->llvm_builder, index_val, step, "iter.next");
      LLVMBuildStore(codegen->llvm_builder, next, index);
      LLVMBuildBr(codegen->llvm_builder, cond_block);

      LLVMPositionBuilderAtEnd(codegen->llvm_builder, end_block);

      codegen_internal_leave_scope(codegen, 1);
    } break;

    case AST_STMT_TYPE_RETURN: {
      LLVMValueRef ret = emit_expr(codegen, ast->expr);
      LLVMBuildStore(codegen->llvm_builder, ret, codegen->retval);
      LLVMBuildBr(codegen->llvm_builder, codegen->return_block);
    } break;

    case AST_STMT_TYPE_STORE: {
      LLVMValueRef lhs = emit_expr(codegen, ast->store.lhs);
      LLVMValueRef rhs = emit_expr(codegen, ast->store.rhs);
      LLVMBuildStore(codegen->llvm_builder, rhs, lhs);
    } break;

    case AST_STMT_TYPE_DEFER: {
      struct defer_entry *entry = calloc(1, sizeof(struct defer_entry));
      entry->expr = ast->expr;
      entry->next = codegen->defer_head;
      codegen->defer_head = entry;
    } break;

    default:
      fprintf(stderr, "unhandled statement type %d\n", ast->type);
  }

  return NULL;
}

void codegen_internal_enter_scope(struct codegen *codegen, struct lex_locator *at,
                                  int lexical_block) {
  codegen->scope = enter_scope(codegen->scope);

  if (!lexical_block) {
    return;
  }

  struct codegen_block *parent = codegen->current_block;
  codegen->current_block = calloc(1, sizeof(struct codegen_block));
  codegen->current_block->parent = parent;
  codegen->current_block->scope_metadata = LLVMDIBuilderCreateLexicalBlock(
      codegen->llvm_dibuilder, parent ? parent->scope_metadata : codegen->current_function_metadata,
      codegen->file_metadata, (unsigned)at->line + 1, (unsigned)at->column + 1);
}

void codegen_internal_leave_scope(struct codegen *codegen, int lexical_block) {
  codegen->scope = exit_scope(codegen->scope);

  if (!lexical_block) {
    return;
  }

  struct codegen_block *block = codegen->current_block;
  codegen->current_block = codegen->current_block->parent;
  free(block);
}
