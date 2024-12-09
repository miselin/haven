
#include "codegen.h"

#include <llvm-c-18/llvm-c/Types.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <malloc.h>
#include <string.h>

#include "ast.h"
#include "kv.h"
#include "scope.h"
#include "tokens.h"
#include "types.h"
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

static LLVMValueRef cast(struct codegen *codegen, LLVMValueRef value, struct ast_ty *from,
                         struct ast_ty *to);

// emit the expression variant of if (i.e. returns a value) - requires else
static LLVMValueRef emit_if(struct codegen *codegen, struct ast_expr *ast);

// emit the statement variant of if (i.e. does not return a value) - does not require else
static void emit_void_if(struct codegen *codegen, struct ast_expr *ast);

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

static LLVMTypeRef ast_ty_to_llvm_ty(struct ast_ty *ty) {
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
          inner = LLVMIntType(ty->integer.width);
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
      inner = LLVMVectorType(LLVMFloatType(), ty->fvec.width);
      break;
    case AST_TYPE_VOID:
      inner = LLVMVoidType();
      break;
    case AST_TYPE_ARRAY:
      inner = LLVMArrayType(ast_ty_to_llvm_ty(ty->array.element_ty), ty->array.width);
      break;
    default:
      fprintf(stderr, "unhandled type %d in conversion to LLVM TypeRef\n", ty->ty);
      return NULL;
  }

  if (ty->flags & TYPE_FLAG_PTR) {
    return LLVMPointerType(inner, 0);
  }

  return inner;
}

static void emit_toplevel(struct codegen *codegen, struct ast_toplevel *ast) {
  if (ast->is_fn) {
    LLVMValueRef func = NULL;
    LLVMTypeRef *param_types = NULL;

    struct scope_entry *entry =
        scope_lookup(codegen->scope, ast->fdecl.ident.value.identv.ident, 0);
    if (!entry) {
      // emit declaration
      param_types = malloc(sizeof(LLVMTypeRef) * ast->fdecl.num_params);
      for (size_t i = 0; i < ast->fdecl.num_params; i++) {
        param_types[i] = ast_ty_to_llvm_ty(&ast->fdecl.params[i]->ty);
      }
      LLVMTypeRef ret_type =
          LLVMFunctionType(ast_ty_to_llvm_ty(&ast->fdecl.retty), param_types, ast->fdecl.num_params,
                           ast->fdecl.flags & DECL_FLAG_VARARG);
      func = LLVMAddFunction(codegen->llvm_module, ast->fdecl.ident.value.identv.ident, ret_type);
      if (ast->fdecl.flags & DECL_FLAG_PUB) {
        LLVMSetLinkage(func, LLVMExternalLinkage);
      } else {
        LLVMSetLinkage(func, LLVMInternalLinkage);
      }

      entry = calloc(1, sizeof(struct scope_entry));
      entry->fdecl = &ast->fdecl;
      entry->function_type = ret_type;
      entry->param_types = param_types;
      entry->ref = func;
      scope_insert(codegen->scope, ast->fdecl.ident.value.identv.ident, entry);
    } else {
      func = entry->ref;
      param_types = entry->param_types;
    }

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
        if (ast->fdecl.retty.ty == AST_TYPE_VOID) {
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
    LLVMTypeRef variable_type = ast_ty_to_llvm_ty(&ast->vdecl.ty);
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

static LLVMValueRef emit_expr(struct codegen *codegen, struct ast_expr *ast) {
  switch (ast->type) {
    case AST_EXPR_TYPE_CONSTANT: {
      LLVMTypeRef const_ty = ast_ty_to_llvm_ty(&ast->ty);
      switch (ast->ty.ty) {
        case AST_TYPE_INTEGER:
          return LLVMConstInt(const_ty, ast->constant.constant.value.intv.val, 0);
        case AST_TYPE_CHAR:
          return LLVMConstInt(const_ty, ast->constant.constant.value.charv.c, 0);
        case AST_TYPE_STRING: {
          LLVMValueRef str = LLVMAddGlobal(
              codegen->llvm_module,
              LLVMArrayType(LLVMInt8Type(), ast->constant.constant.value.strv.length + 1), "str");
          LLVMSetInitializer(str, LLVMConstString(ast->constant.constant.value.strv.s,
                                                  ast->constant.constant.value.strv.length, 0));
          return str;
        } break;
        case AST_TYPE_FVEC: {
          LLVMValueRef *fields = malloc(sizeof(LLVMValueRef) * ast->list->num_elements);

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
            LLVMValueRef zero = LLVMConstNull(LLVMVectorType(LLVMFloatType(), i));

            // add zero to the vector to get it into a temporary
            LLVMValueRef vec_stack = new_alloca(codegen, ast_ty_to_llvm_ty(&ast->ty), "vec");
            LLVMBuildStore(codegen->llvm_builder, zero, vec_stack);
            LLVMValueRef vec = LLVMBuildLoad2(codegen->llvm_builder, ast_ty_to_llvm_ty(&ast->ty),
                                              vec_stack, "vec");
            for (size_t j = 0; j < i; j++) {
              LLVMValueRef idx = LLVMConstInt(LLVMInt32Type(), j, 0);
              vec = LLVMBuildInsertElement(codegen->llvm_builder, vec, fields[j], idx, "element");
            }
            free(fields);
            return vec;
          }

          LLVMValueRef vec = LLVMConstVector(fields, i);
          free(fields);
          return vec;

        } break;
        case AST_TYPE_FLOAT:
          return LLVMConstRealOfStringAndSize(LLVMFloatType(),
                                              ast->constant.constant.value.floatv.buf,
                                              ast->constant.constant.value.floatv.length);

        case AST_TYPE_ARRAY: {
          LLVMTypeRef inner_ty = ast_ty_to_llvm_ty(ast->ty.array.element_ty);
          LLVMValueRef *values = malloc(sizeof(LLVMValueRef) * ast->list->num_elements);
          struct ast_expr_list *node = ast->list;
          for (size_t i = 0; i < ast->list->num_elements; i++) {
            values[i] = emit_expr(codegen, node->expr);
            values[i] = cast(codegen, values[i], &node->expr->ty, ast->ty.array.element_ty);

            node = node->next;
          }
          LLVMValueRef array = LLVMConstArray2(inner_ty, values, ast->list->num_elements);
          free(values);
          return array;
        } break;

        default: {
          char buf[256];
          type_name_into(&ast->ty, buf, 256);
          fprintf(stderr, "unhandled constant type %s\n", buf);
        }
      }
    } break;

    case AST_EXPR_TYPE_VARIABLE: {
      struct scope_entry *lookup =
          scope_lookup(codegen->scope, ast->variable.ident.value.identv.ident, 1);

      // temporaries are things like function parameters, and do not require loads
      if (lookup->vdecl->flags & DECL_FLAG_TEMPORARY) {
        return lookup->ref;
      } else if (ast->ty.flags & TYPE_FLAG_PTR) {
        // if we WANT a pointer, don't load it
        return lookup->ref;
      }

      return LLVMBuildLoad2(codegen->llvm_builder, lookup->variable_type, lookup->ref,
                            ast->variable.ident.value.identv.ident);
    } break;

    case AST_EXPR_TYPE_BINARY: {
      LLVMValueRef lhs = emit_expr(codegen, ast->binary.lhs);
      LLVMValueRef rhs = emit_expr(codegen, ast->binary.rhs);

      if (ast->ty.ty == AST_TYPE_FVEC) {
        size_t element_count = ast->ty.fvec.width;
        if (ast->binary.lhs->ty.ty != ast->binary.rhs->ty.ty) {
          // TODO: order of ops, find which one is the scalar broadcast vector
          LLVMTypeRef vecty = LLVMVectorType(LLVMFloatType(), element_count);
          LLVMValueRef zero = LLVMConstNull(vecty);
          LLVMValueRef undef = LLVMGetUndef(vecty);
          LLVMValueRef bvec = LLVMBuildInsertElement(
              codegen->llvm_builder, undef, rhs, LLVMConstInt(LLVMInt32Type(), 0, 0), "broadcast");
          rhs = LLVMBuildShuffleVector(codegen->llvm_builder, bvec, undef, zero, "shuffle");
        }
      }

      if (ast->ty.ty == AST_TYPE_FLOAT || ast->ty.ty == AST_TYPE_FVEC) {
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
          case AST_BINARY_OP_LSHIFT:
            return LLVMBuildShl(codegen->llvm_builder, lhs, rhs, "shl");
          case AST_BINARY_OP_RSHIFT:
            return LLVMBuildAShr(codegen->llvm_builder, lhs, rhs, "shr");
        }
      }

      fprintf(stderr, "unhandled binary op %d\n", ast->binary.op);
    } break;

    case AST_EXPR_TYPE_LOGICAL: {
      LLVMTypeRef lhs_type = ast_ty_to_llvm_ty(&ast->logical.lhs->ty);
      LLVMTypeRef rhs_type = ast_ty_to_llvm_ty(&ast->logical.rhs->ty);

      // const char *name = ast->logical.op == AST_LOGICAL_OP_AND ? "and" : "or";
      LLVMValueRef lhs = emit_expr(codegen, ast->logical.lhs);
      LLVMValueRef lcmp =
          LLVMBuildICmp(codegen->llvm_builder, LLVMIntNE, lhs, LLVMConstInt(lhs_type, 0, 0), "lhs");

      LLVMContextRef context = LLVMGetGlobalContext();

      LLVMBasicBlockRef start = LLVMGetInsertBlock(codegen->llvm_builder);
      LLVMBasicBlockRef rhs = LLVMCreateBasicBlockInContext(context, "logic.rhs");
      LLVMBasicBlockRef end = LLVMCreateBasicBlockInContext(context, "logic.end");

      LLVMBuildCondBr(codegen->llvm_builder, lcmp, rhs, end);

      LLVMAppendExistingBasicBlock(codegen->current_function, rhs);
      LLVMPositionBuilderAtEnd(codegen->llvm_builder, rhs);
      LLVMValueRef rhs_val = emit_expr(codegen, ast->logical.rhs);
      LLVMValueRef rcmp = LLVMBuildICmp(codegen->llvm_builder, LLVMIntNE, rhs_val,
                                        LLVMConstInt(rhs_type, 0, 0), "rhs");
      LLVMBasicBlockRef final_rhs = LLVMGetInsertBlock(codegen->llvm_builder);
      LLVMBuildBr(codegen->llvm_builder, end);

      LLVMAppendExistingBasicBlock(codegen->current_function, end);
      LLVMPositionBuilderAtEnd(codegen->llvm_builder, end);
      LLVMValueRef phi = LLVMBuildPhi(codegen->llvm_builder, LLVMInt1Type(), "phi");
      LLVMValueRef values[] = {LLVMConstInt(LLVMInt1Type(), 0, 0), rcmp};
      LLVMBasicBlockRef blocks[] = {start, final_rhs};
      LLVMAddIncoming(phi, values, blocks, 2);

      return phi;
    } break;

    case AST_EXPR_TYPE_BLOCK: {
      return emit_block(codegen, &ast->block);
    } break;

    case AST_EXPR_TYPE_CALL: {
      struct scope_entry *entry =
          scope_lookup(codegen->scope, ast->call.ident.value.identv.ident, 1);

      size_t named_param_count = LLVMCountParams(entry->ref);

      LLVMValueRef *args = NULL;
      size_t num_args = 0;
      if (ast->call.args) {
        args = malloc(sizeof(LLVMValueRef) * ast->call.args->num_elements);
        struct ast_expr_list *node = ast->call.args;
        while (node) {
          args[num_args] = emit_expr(codegen, node->expr);
          if (node->expr->ty.ty == AST_TYPE_FLOAT && num_args >= named_param_count) {
            // vararg floats need to be promoted to doubles for C compatibility
            args[num_args] =
                LLVMBuildFPExt(codegen->llvm_builder, args[num_args], LLVMDoubleType(), "fpext");
          }
          ++num_args;
          node = node->next;
        }
      }

      LLVMValueRef call = LLVMBuildCall2(codegen->llvm_builder, entry->function_type, entry->ref,
                                         args, num_args, "");
      if (args) {
        free(args);
      }
      return call;
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

      LLVMValueRef index = LLVMConstInt(LLVMInt64Type(), ast->deref.field, 0);
      return LLVMBuildExtractElement(codegen->llvm_builder, ref, index, "deref");
    }; break;

    case AST_EXPR_TYPE_VOID:
      return NULL;

    case AST_EXPR_TYPE_CAST: {
      LLVMValueRef expr = emit_expr(codegen, ast->cast.expr);
      return cast(codegen, expr, &ast->cast.expr->ty, &ast->ty);
    } break;

    case AST_EXPR_TYPE_IF: {
      if (ast->ty.ty == AST_TYPE_VOID) {
        emit_void_if(codegen, ast);
        return NULL;
      } else {
        return emit_if(codegen, ast);
      }
    } break;

    case AST_EXPR_TYPE_ASSIGN: {
      LLVMValueRef expr = emit_expr(codegen, ast->assign.expr);

      struct scope_entry *entry =
          scope_lookup(codegen->scope, ast->call.ident.value.identv.ident, 1);

      if (entry->vdecl->flags & DECL_FLAG_TEMPORARY) {
        // swap the reference to this expression from now on
        entry->ref = expr;
        return expr;
      } else {
        LLVMBuildStore(codegen->llvm_builder, expr, entry->ref);
      }

      return expr;
    } break;

    case AST_EXPR_TYPE_REF: {
      return emit_expr(codegen, ast->ref.expr);
    } break;

    case AST_EXPR_TYPE_LOAD: {
      LLVMValueRef expr = emit_expr(codegen, ast->load.expr);
      LLVMTypeRef expr_type = ast_ty_to_llvm_ty(&ast->ty);

      return LLVMBuildLoad2(codegen->llvm_builder, expr_type, expr,
                            ast->call.ident.value.identv.ident);
    } break;

    case AST_EXPR_TYPE_UNARY: {
      LLVMValueRef expr = emit_expr(codegen, ast->unary.expr);
      switch (ast->unary.op) {
        case AST_UNARY_OP_NEG:
          if (ast->ty.ty == AST_TYPE_FLOAT) {
            return LLVMBuildFNeg(codegen->llvm_builder, expr, "fneg");
          } else {
            return LLVMBuildNeg(codegen->llvm_builder, expr, "neg");
          }
        case AST_UNARY_OP_NOT:
          return LLVMBuildNot(codegen->llvm_builder, expr, "not");
        case AST_UNARY_OP_COMP:
          return LLVMBuildXor(codegen->llvm_builder, expr, LLVMConstInt(LLVMInt32Type(), -1, 0),
                              "comp");
      }
    } break;

    case AST_EXPR_TYPE_BOOLEAN: {
      LLVMIntPredicate iop;
      LLVMRealPredicate fop;
      switch (ast->boolean.op) {
        case TOKEN_EQUALS:
          iop = LLVMIntEQ;
          fop = LLVMRealOEQ;
          break;
        case TOKEN_NE:
          iop = LLVMIntNE;
          fop = LLVMRealONE;
          break;
        case TOKEN_LT:
          iop = LLVMIntSLT;
          fop = LLVMRealOLT;
          break;
        case TOKEN_GT:
          iop = LLVMIntSGT;
          fop = LLVMRealOGT;
          break;
        case TOKEN_LTE:
          iop = LLVMIntSLE;
          fop = LLVMRealOLE;
          break;
        case TOKEN_GTE:
          iop = LLVMIntSGE;
          fop = LLVMRealOGE;
          break;
        default:
          fprintf(stderr, "unhandled boolean op %d\n", ast->boolean.op);
          return NULL;
      }

      LLVMValueRef lhs = emit_expr(codegen, ast->boolean.lhs);
      LLVMValueRef rhs = emit_expr(codegen, ast->boolean.rhs);
      if (ast->boolean.lhs->ty.ty == AST_TYPE_FLOAT) {
        return LLVMBuildFCmp(codegen->llvm_builder, fop, lhs, rhs, "cmp");
      }
      return LLVMBuildICmp(codegen->llvm_builder, iop, lhs, rhs, "cmp");
    } break;

    case AST_EXPR_TYPE_ARRAY_INDEX: {
      struct scope_entry *entry =
          scope_lookup(codegen->scope, ast->array_index.ident.value.identv.ident, 1);

      LLVMTypeRef target_ty = ast_ty_to_llvm_ty(&ast->ty);

      LLVMValueRef index = emit_expr(codegen, ast->array_index.index);
      LLVMValueRef gep[] = {LLVMConstInt(LLVMInt32Type(), 0, 0), index};
      LLVMValueRef retrieve =
          LLVMBuildGEP2(codegen->llvm_builder, entry->variable_type, entry->ref, gep, 2, "");
      return LLVMBuildLoad2(codegen->llvm_builder, target_ty, retrieve, "load");
    } break;

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

static LLVMValueRef cast(struct codegen *codegen, LLVMValueRef value, struct ast_ty *from,
                         struct ast_ty *to) {
  if (same_type(from, to)) {
    return value;
  }

  LLVMTypeRef dest_ty = ast_ty_to_llvm_ty(to);

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

static LLVMValueRef emit_if(struct codegen *codegen, struct ast_expr *ast) {
  LLVMValueRef cond_expr = emit_expr(codegen, ast->if_expr.cond);
  LLVMValueRef cond =
      LLVMBuildICmp(codegen->llvm_builder, LLVMIntNE, cond_expr,
                    LLVMConstInt(ast_ty_to_llvm_ty(&ast->if_expr.cond->ty), 0, 0), "tobool");

  LLVMContextRef context = LLVMGetGlobalContext();

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
  LLVMValueRef phi = LLVMBuildPhi(codegen->llvm_builder, ast_ty_to_llvm_ty(&ast->ty), "phi");
  LLVMValueRef values[] = {then_val, else_val};
  LLVMBasicBlockRef blocks[] = {final_then_block, final_else_block};
  LLVMAddIncoming(phi, values, blocks, 2);

  return phi;
}

static void emit_void_if(struct codegen *codegen, struct ast_expr *ast) {
  LLVMValueRef cond_expr = emit_expr(codegen, ast->if_expr.cond);
  LLVMValueRef cond =
      LLVMBuildICmp(codegen->llvm_builder, LLVMIntNE, cond_expr,
                    LLVMConstInt(ast_ty_to_llvm_ty(&ast->if_expr.cond->ty), 0, 0), "tobool");

  LLVMContextRef context = LLVMGetGlobalContext();

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
