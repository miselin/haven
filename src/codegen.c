
#include "codegen.h"

#include <llvm-c-18/llvm-c/Types.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <malloc.h>
#include <string.h>

#include "ast.h"
#include "kv.h"
#include "scope.h"
#include "utility.h"

struct scope_entry {
  struct ast_vdecl *vdecl;
  struct ast_fdecl *fdecl;

  LLVMTypeRef function_type;
  LLVMTypeRef *param_types;
  LLVMTypeRef variable_type;

  LLVMValueRef ref;
};

struct codegen {
  struct ast_program *ast;

  LLVMContextRef llvm_context;
  LLVMModuleRef llvm_module;
  LLVMBuilderRef llvm_builder;

  LLVMValueRef current_function;
  LLVMBasicBlockRef entry_block;
  LLVMValueRef last_alloca;

  // name -> LLVMValueRef
  struct kv *locals;

  // name -> LLVMTypeRef for declared functions
  struct kv *functions;

  // we keep a scope to track LLVM refs for variables/functions
  struct scope *scope;
};

static void emit_ast(struct codegen *codegen, struct ast_program *ast);
static void emit_toplevel(struct codegen *codegen, struct ast_toplevel *ast);
static LLVMValueRef emit_block(struct codegen *codegen, struct ast_block *ast);
static LLVMValueRef emit_stmt(struct codegen *codegen, struct ast_stmt *ast);
static LLVMValueRef emit_expr(struct codegen *codegen, struct ast_expr *ast);

static LLVMValueRef new_alloca(struct codegen *codegen, LLVMTypeRef type, const char *name);

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

static LLVMTypeRef ast_ty_to_llvm_ty(enum ast_ty ty) {
  // TODO: bit width stuff
  switch (ty) {
    case AST_TYPE_INTEGER:
      return LLVMInt32Type();
    case AST_TYPE_CHAR:
      return LLVMInt8Type();
    case AST_TYPE_STRING:
      return LLVMPointerType(LLVMInt8Type(), 0);
    case AST_TYPE_FLOAT:
      return LLVMFloatType();
    case AST_TYPE_FVEC:
      return LLVMVectorType(LLVMFloatType(), 3);
    case AST_TYPE_VOID:
      return LLVMVoidType();
    default:
      fprintf(stderr, "unhandled type %d\n", ty);
  }

  return NULL;
}

static void emit_toplevel(struct codegen *codegen, struct ast_toplevel *ast) {
  if (ast->is_fn) {
    // emit declaration
    LLVMTypeRef *param_types = malloc(sizeof(LLVMTypeRef) * ast->fdecl.num_params);
    for (size_t i = 0; i < ast->fdecl.num_params; i++) {
      param_types[i] = ast_ty_to_llvm_ty(ast->fdecl.params[i]->ty);
    }
    LLVMTypeRef ret_type =
        LLVMFunctionType(ast_ty_to_llvm_ty(ast->fdecl.retty), param_types, ast->fdecl.num_params,
                         ast->fdecl.flags & DECL_FLAG_VARARG);
    LLVMValueRef func =
        LLVMAddFunction(codegen->llvm_module, ast->fdecl.ident.value.identv.ident, ret_type);
    if (ast->fdecl.flags & DECL_FLAG_PUB) {
      LLVMSetLinkage(func, LLVMExternalLinkage);
    } else {
      LLVMSetLinkage(func, LLVMInternalLinkage);
    }

    struct scope_entry *entry = calloc(1, sizeof(struct scope_entry));
    entry->fdecl = &ast->fdecl;
    entry->function_type = ret_type;
    entry->param_types = param_types;
    entry->ref = func;
    scope_insert(codegen->scope, ast->fdecl.ident.value.identv.ident, entry);

    // generate definition if we have one
    if (ast->fdecl.body) {
      codegen->entry_block = LLVMAppendBasicBlock(func, "entry");
      codegen->last_alloca = NULL;
      LLVMPositionBuilderAtEnd(codegen->llvm_builder, codegen->entry_block);

      codegen->current_function = func;
      codegen->locals = new_kv();

      codegen->scope = enter_scope(codegen->scope);

      for (size_t i = 0; i < ast->fdecl.num_params; i++) {
        struct scope_entry *entry = calloc(1, sizeof(struct scope_entry));
        entry->vdecl = ast->fdecl.params[i];
        entry->variable_type = param_types[i];
        entry->ref = LLVMGetParam(func, i);
        scope_insert(codegen->scope, ast->fdecl.params[i]->ident.value.identv.ident, entry);
      }

      LLVMValueRef block_result = emit_block(codegen, ast->fdecl.body);
      if (block_result) {
        LLVMBuildRet(codegen->llvm_builder, block_result);
      } else {
        if (ast->fdecl.retty == AST_TYPE_VOID) {
          LLVMBuildRetVoid(codegen->llvm_builder);
        } else {
          // semantic analysis should catch this case, but handle it just in case
          // TODO: need to generate a return value based on the function's return type
          LLVMBuildRet(codegen->llvm_builder, LLVMConstInt(LLVMInt32Type(), 0, 0));
        }
      }

      codegen->scope = exit_scope(codegen->scope);

      destroy_kv(codegen->locals);
      codegen->current_function = NULL;
    }
  } else {
    // emit global variable
    LLVMTypeRef variable_type = ast_ty_to_llvm_ty(ast->vdecl.ty);
    LLVMValueRef global =
        LLVMAddGlobal(codegen->llvm_module, variable_type, ast->vdecl.ident.value.identv.ident);
    if (ast->vdecl.flags & DECL_FLAG_PUB) {
      LLVMSetLinkage(global, LLVMExternalLinkage);
    } else {
      LLVMSetLinkage(global, LLVMInternalLinkage);
    }

    struct scope_entry *entry = calloc(1, sizeof(struct scope_entry));
    entry->vdecl = &ast->vdecl;
    entry->variable_type = variable_type;
    entry->ref = global;
    scope_insert(codegen->scope, ast->vdecl.ident.value.identv.ident, entry);

    if (ast->vdecl.init_expr) {
      LLVMSetInitializer(global, emit_expr(codegen, ast->vdecl.init_expr));
    }
  }
}

static LLVMValueRef emit_block(struct codegen *codegen, struct ast_block *ast) {
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
    case AST_STMT_TYPE_EXPR:
      return emit_expr(codegen, ast->expr);

    case AST_STMT_TYPE_LET: {
      const char *ident = ast->let.ident.value.identv.ident;
      LLVMTypeRef var_type = ast_ty_to_llvm_ty(ast->let.ty);
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

    default:
      fprintf(stderr, "unhandled statement type %d\n", ast->type);
  }

  return NULL;
}

static LLVMValueRef emit_expr(struct codegen *codegen, struct ast_expr *ast) {
  switch (ast->type) {
    case AST_EXPR_TYPE_CONSTANT: {
      switch (ast->ty) {
        case AST_TYPE_INTEGER:
          return LLVMConstInt(LLVMInt32Type(), ast->constant.constant.value.intv.val, 0);
        case AST_TYPE_CHAR:
          return LLVMConstInt(LLVMInt8Type(), ast->constant.constant.value.charv.c, 0);
        case AST_TYPE_STRING: {
          LLVMValueRef str = LLVMAddGlobal(
              codegen->llvm_module,
              LLVMArrayType(LLVMInt8Type(), ast->constant.constant.value.strv.length + 1), "str");
          LLVMSetInitializer(str, LLVMConstString(ast->constant.constant.value.strv.s,
                                                  ast->constant.constant.value.strv.length, 0));
          return str;
        } break;
        case AST_TYPE_FVEC: {
          // TODO: need the # of elements??
          LLVMValueRef fields[3];
          size_t i = 0;
          int non_const_elements = 0;
          struct ast_expr_list *node = ast->list;
          while (node) {
            fields[i++] = emit_expr(codegen, node->expr);
            if (node->expr->type != AST_EXPR_TYPE_CONSTANT) {
              non_const_elements++;
            }
            node = node->next;
          }

          if (non_const_elements) {
            // add zero to the vector to get it into a temporary
            LLVMValueRef zero = LLVMConstNull(LLVMFloatType());
            LLVMValueRef vec_stack = new_alloca(codegen, ast_ty_to_llvm_ty(ast->ty), "vec");
            LLVMBuildStore(codegen->llvm_builder,
                           LLVMConstVector((LLVMValueRef[]){zero, zero, zero}, 3), vec_stack);
            LLVMValueRef vec =
                LLVMBuildLoad2(codegen->llvm_builder, ast_ty_to_llvm_ty(ast->ty), vec_stack, "vec");
            for (size_t j = 0; j < i; j++) {
              LLVMValueRef idx = LLVMConstInt(LLVMInt32Type(), j, 0);
              vec = LLVMBuildInsertElement(codegen->llvm_builder, vec, fields[j], idx, "element");
            }
            return vec;
          }

          return LLVMConstVector(fields, i);

        } break;
        case AST_TYPE_FLOAT:
          return LLVMConstRealOfStringAndSize(LLVMFloatType(),
                                              ast->constant.constant.value.floatv.buf,
                                              ast->constant.constant.value.floatv.length);

        default:
          fprintf(stderr, "unhandled constant type %d\n", ast->constant.constant.ident);
      }
    } break;

    case AST_EXPR_TYPE_VARIABLE: {
      struct scope_entry *lookup =
          scope_lookup(codegen->scope, ast->variable.ident.value.identv.ident, 1);

      // temporaries are things like function parameters, and do not require loads
      if (lookup->vdecl->flags & DECL_FLAG_TEMPORARY) {
        return lookup->ref;
      }

      return LLVMBuildLoad2(codegen->llvm_builder, lookup->variable_type, lookup->ref,
                            ast->variable.ident.value.identv.ident);
    } break;

    case AST_EXPR_TYPE_BINARY: {
      LLVMValueRef lhs = emit_expr(codegen, ast->binary.lhs);
      LLVMValueRef rhs = emit_expr(codegen, ast->binary.rhs);

      if (ast->ty == AST_TYPE_FVEC) {
        if (ast->binary.lhs->ty != ast->binary.rhs->ty) {
          // TODO: order of ops, find which one is the scalar
          // broadcast vector
          LLVMValueRef zero = LLVMConstInt(LLVMInt32Type(), 0, 0);
          LLVMValueRef bvec = LLVMBuildInsertElement(
              codegen->llvm_builder, LLVMGetUndef(LLVMVectorType(LLVMFloatType(), 3)), rhs,
              LLVMConstInt(LLVMInt32Type(), 0, 0), "broadcast");
          LLVMValueRef shuffled = LLVMBuildShuffleVector(
              codegen->llvm_builder, bvec, LLVMGetUndef(LLVMVectorType(LLVMFloatType(), 3)),
              LLVMConstVector((LLVMValueRef[]){zero, zero, zero}, 3), "shuffle");
          rhs = shuffled;
        }
      }

      if (ast->ty == AST_TYPE_FLOAT || ast->ty == AST_TYPE_FVEC) {
        switch (ast->binary.op) {
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
        switch (ast->binary.op) {
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
        }
      }

      fprintf(stderr, "unhandled binary op %d\n", ast->binary.op);
    } break;

    case AST_EXPR_TYPE_LOGICAL: {
      LLVMBasicBlockRef start = LLVMGetInsertBlock(codegen->llvm_builder);
      LLVMBasicBlockRef rhs = LLVMAppendBasicBlock(codegen->current_function, "rhs");
      LLVMBasicBlockRef end = LLVMAppendBasicBlock(codegen->current_function, "end");

      // const char *name = ast->logical.op == AST_LOGICAL_OP_AND ? "and" :
      // "or";
      LLVMValueRef lhs = emit_expr(codegen, ast->logical.lhs);
      LLVMValueRef lcmp = LLVMBuildICmp(codegen->llvm_builder, LLVMIntNE, lhs,
                                        LLVMConstInt(LLVMInt32Type(), 0, 0), "lhs");
      LLVMBuildCondBr(codegen->llvm_builder, lcmp, rhs, end);

      LLVMPositionBuilderAtEnd(codegen->llvm_builder, rhs);
      LLVMValueRef rhs_val = emit_expr(codegen, ast->logical.rhs);
      LLVMValueRef rcmp = LLVMBuildICmp(codegen->llvm_builder, LLVMIntNE, rhs_val,
                                        LLVMConstInt(LLVMInt32Type(), 0, 0), "rhs");
      LLVMBuildBr(codegen->llvm_builder, end);

      LLVMPositionBuilderAtEnd(codegen->llvm_builder, end);
      LLVMValueRef phi = LLVMBuildPhi(codegen->llvm_builder, LLVMInt1Type(), "phi");
      LLVMValueRef values[] = {LLVMConstInt(LLVMInt1Type(), 0, 0), rcmp};
      LLVMBasicBlockRef blocks[] = {start, rhs};
      LLVMAddIncoming(phi, values, blocks, 2);

      return phi;
    } break;

    case AST_EXPR_TYPE_BLOCK: {
      return emit_block(codegen, &ast->block);
    } break;

    case AST_EXPR_TYPE_CALL: {
      LLVMValueRef *args = malloc(sizeof(LLVMValueRef) * ast->call.args->num_elements);
      struct ast_expr_list *node = ast->call.args;
      size_t i = 0;
      while (node) {
        args[i++] = emit_expr(codegen, node->expr);
        node = node->next;
      }

      struct scope_entry *entry =
          scope_lookup(codegen->scope, ast->call.ident.value.identv.ident, 1);

      fprintf(stderr, "trying to find %s\n", ast->call.ident.value.identv.ident);

      for (size_t j = LLVMCountParams(entry->ref); j < i; ++j) {
        // promote for C variadic compatibility
        args[j] = LLVMBuildFPExt(codegen->llvm_builder, args[j], LLVMDoubleType(), "fpext");
      }

      return LLVMBuildCall2(codegen->llvm_builder, entry->function_type, entry->ref, args,
                            ast->call.args->num_elements, "call");
    } break;

    case AST_EXPR_TYPE_DEREF: {
      struct scope_entry *entry =
          scope_lookup(codegen->scope, ast->call.ident.value.identv.ident, 1);

      LLVMValueRef ref;
      if (entry->vdecl->flags & DECL_FLAG_TEMPORARY) {
        ref = entry->ref;
      } else {
        ref = LLVMBuildLoad2(codegen->llvm_builder, entry->variable_type, entry->ref,
                             ast->call.ident.value.identv.ident);
      }

      LLVMValueRef index;
      if (!strcmp(ast->deref.field.value.identv.ident, "x")) {
        index = LLVMConstInt(LLVMInt32Type(), 0, 0);
      } else if (!strcmp(ast->deref.field.value.identv.ident, "y")) {
        index = LLVMConstInt(LLVMInt32Type(), 1, 0);
      } else if (!strcmp(ast->deref.field.value.identv.ident, "z")) {
        index = LLVMConstInt(LLVMInt32Type(), 2, 0);
      } else {
        fprintf(stderr, "unhandled field %s\n", ast->deref.field.value.identv.ident);
        return NULL;
      }

      return LLVMBuildExtractElement(codegen->llvm_builder, ref, index, "deref");
    }; break;

    case AST_EXPR_TYPE_VOID:
      return NULL;

    default:
      fprintf(stderr, "unhandled expression type %d\n", ast->type);
  }

  return NULL;
}

static LLVMValueRef new_alloca(struct codegen *codegen, LLVMTypeRef type, const char *name) {
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
