#include "typecheck.h"

#include <malloc.h>

#include "ast.h"
#include "scope.h"
#include "utility.h"

struct scope_entry {
  struct ast_vdecl *vdecl;
  struct ast_fdecl *fdecl;
};

struct typecheck {
  struct ast_program *ast;

  struct scope *scope;

  int errors;
};

static void typecheck_ast(struct typecheck *typecheck, struct ast_program *ast);
static void typecheck_toplevel(struct typecheck *typecheck, struct ast_toplevel *ast);
static enum ast_ty typecheck_block(struct typecheck *typecheck, struct ast_block *ast);
static enum ast_ty typecheck_stmt(struct typecheck *typecheck, struct ast_stmt *ast);
static enum ast_ty typecheck_expr(struct typecheck *typecheck, struct ast_expr *ast);

static int binary_mismatch_ok(int op, enum ast_ty lhs, enum ast_ty rhs);

struct typecheck *new_typecheck(struct ast_program *ast) {
  struct typecheck *result = calloc(1, sizeof(struct typecheck));
  result->ast = ast;
  result->scope = enter_scope(NULL);
  return result;
}

int typecheck_run(struct typecheck *typecheck) {
  typecheck_ast(typecheck, typecheck->ast);
  return typecheck->errors;
}

void destroy_typecheck(struct typecheck *typecheck) {
  exit_scope(typecheck->scope);
  free(typecheck);
}

static void typecheck_ast(struct typecheck *typecheck, struct ast_program *ast) {
  struct ast_toplevel *decl = ast->decls;
  while (decl) {
    typecheck_toplevel(typecheck, decl);
    decl = decl->next;
  }
}

static void typecheck_toplevel(struct typecheck *typecheck, struct ast_toplevel *ast) {
  if (ast->is_fn) {
    struct scope_entry *existing =
        scope_lookup(typecheck->scope, ast->fdecl.ident.value.identv.ident, 1);

    struct scope_entry *entry = calloc(1, sizeof(struct scope_entry));
    entry->fdecl = &ast->fdecl;

    scope_insert(typecheck->scope, ast->fdecl.ident.value.identv.ident, entry);

    if (existing) {
      if (entry->fdecl->retty != existing->fdecl->retty) {
        fprintf(stderr, "function %s redeclared with different return type %d, expected %d\n",
                ast->fdecl.ident.value.identv.ident, entry->fdecl->retty, existing->fdecl->retty);
        ++typecheck->errors;
      }

      if (entry->fdecl->num_params != existing->fdecl->num_params) {
        fprintf(stderr,
                "function %s redeclared with different number of parameters %zu, expected %zu\n",
                ast->fdecl.ident.value.identv.ident, entry->fdecl->num_params,
                existing->fdecl->num_params);
        ++typecheck->errors;
      }

      for (size_t i = 0; i < entry->fdecl->num_params; ++i) {
        if (entry->fdecl->params[i]->ty != existing->fdecl->params[i]->ty) {
          fprintf(stderr, "function %s parameter %zu has type %d, expected %d\n",
                  ast->fdecl.ident.value.identv.ident, i, entry->fdecl->params[i]->ty,
                  existing->fdecl->params[i]->ty);
          ++typecheck->errors;
        }
      }

      // done with the old entry, new definition is compatible
      free(existing);
    }

    if (ast->fdecl.body) {
      typecheck->scope = enter_scope(typecheck->scope);

      // declare the parameters in the function scope
      for (size_t i = 0; i < ast->fdecl.num_params; i++) {
        struct scope_entry *entry = calloc(1, sizeof(struct scope_entry));
        entry->vdecl = ast->fdecl.params[i];
        scope_insert(typecheck->scope, ast->fdecl.params[i]->ident.value.identv.ident, entry);
      }

      enum ast_ty result = typecheck_block(typecheck, ast->fdecl.body);

      typecheck->scope = exit_scope(typecheck->scope);

      if (result == AST_TYPE_ERROR) {
        return;
      }

      if (result != ast->fdecl.retty) {
        fprintf(stderr, "function %s returns %d, expected %d\n",
                ast->fdecl.ident.value.identv.ident, result, ast->fdecl.retty);
        ++typecheck->errors;
      }
    }
  } else {
    if (ast->vdecl.init_expr) {
      struct scope_entry *existing =
          scope_lookup(typecheck->scope, ast->vdecl.ident.value.identv.ident, 0);
      if (existing) {
        fprintf(stderr, "typecheck: multiple definitions of variable %s\n",
                ast->vdecl.ident.value.identv.ident);
        ++typecheck->errors;
        return;
      }

      struct scope_entry *entry = calloc(1, sizeof(struct scope_entry));
      entry->vdecl = &ast->vdecl;

      // insert before checking the initializer to allow recursive references
      scope_insert(typecheck->scope, ast->vdecl.ident.value.identv.ident, entry);

      enum ast_ty result = typecheck_expr(typecheck, ast->vdecl.init_expr);
      if (result == AST_TYPE_ERROR) {
        return;
      }

      if (result != ast->vdecl.ty) {
        fprintf(stderr, "variable %s initializer has type %d, expected %d\n",
                ast->vdecl.ident.value.identv.ident, result, ast->vdecl.ty);
        ++typecheck->errors;
        return;
      }
    }
  }
}

static enum ast_ty typecheck_block(struct typecheck *typecheck, struct ast_block *ast) {
  typecheck->scope = enter_scope(typecheck->scope);

  struct ast_stmt *stmt = ast->stmt;
  enum ast_ty last_ty = AST_TYPE_VOID;
  while (stmt) {
    enum ast_ty ty = typecheck_stmt(typecheck, stmt);
    if (!stmt->next) {
      last_ty = ty;
    }
    stmt = stmt->next;
  }

  typecheck->scope = exit_scope(typecheck->scope);
  return last_ty;
}

static enum ast_ty typecheck_stmt(struct typecheck *typecheck, struct ast_stmt *ast) {
  switch (ast->type) {
    case AST_STMT_TYPE_EXPR:
      return typecheck_expr(typecheck, ast->expr);

    case AST_STMT_TYPE_LET: {
      struct scope_entry *entry = calloc(1, sizeof(struct scope_entry));
      entry->vdecl = &ast->let;

      // insert before checking the initializer to allow recursive references
      scope_insert(typecheck->scope, ast->let.ident.value.identv.ident, entry);

      enum ast_ty init_ty = typecheck_expr(typecheck, ast->let.init_expr);
      if (ast->let.ty == AST_TYPE_TBD) {
        // inferred type
        ast->let.ty = init_ty;
      }

      if (ast->let.ty != init_ty) {
        fprintf(stderr, "let %s initializer has type %d, expected %d\n",
                ast->let.ident.value.identv.ident, init_ty, ast->let.ty);
        ++typecheck->errors;
      }
    } break;

    default:
      fprintf(stderr, "typecheck: unhandled statement type %d\n", ast->type);
  }

  // statements that aren't expressions do not have types (their expressions do)
  return AST_TYPE_VOID;
}

static enum ast_ty typecheck_expr(struct typecheck *typecheck, struct ast_expr *ast) {
  switch (ast->type) {
    case AST_EXPR_TYPE_CONSTANT: {
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_VARIABLE: {
      struct scope_entry *entry =
          scope_lookup(typecheck->scope, ast->variable.ident.value.identv.ident, 1);
      if (!entry || !entry->vdecl) {
        fprintf(stderr, "%s not found or not a variable\n", ast->variable.ident.value.identv.ident);
        ++typecheck->errors;
        return AST_TYPE_ERROR;
      }
      ast->ty = entry->vdecl->ty;
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_BINARY: {
      enum ast_ty lhs = typecheck_expr(typecheck, ast->binary.lhs);
      enum ast_ty rhs = typecheck_expr(typecheck, ast->binary.rhs);

      if (lhs != rhs && !binary_mismatch_ok(ast->binary.op, lhs, rhs)) {
        fprintf(stderr, "binary op %d has mismatching lhs type %d, rhs type %d\n", ast->binary.op,
                lhs, rhs);
        ++typecheck->errors;
        return AST_TYPE_ERROR;
      }

      ast->ty = lhs;
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_LOGICAL: {
      enum ast_ty lhs = typecheck_expr(typecheck, ast->logical.lhs);
      enum ast_ty rhs = typecheck_expr(typecheck, ast->logical.rhs);

      if (lhs != rhs) {
        fprintf(stderr, "logical op %d has mismatching lhs type %d, rhs type %d\n", ast->logical.op,
                lhs, rhs);
        ++typecheck->errors;
        return AST_TYPE_ERROR;
      }

      // return type of this operation is actually a 1-bit boolean
      ast->ty = AST_TYPE_INTEGER;
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_BLOCK: {
      return typecheck_block(typecheck, &ast->block);
    } break;

    case AST_EXPR_TYPE_CALL: {
      struct scope_entry *entry =
          scope_lookup(typecheck->scope, ast->variable.ident.value.identv.ident, 1);
      if (!entry || !entry->fdecl) {
        fprintf(stderr, "%s not found or not a function\n", ast->variable.ident.value.identv.ident);
        ++typecheck->errors;
        return AST_TYPE_ERROR;
      }

      if (entry->fdecl->num_params != ast->call.args->num_elements) {
        if ((entry->fdecl->flags & DECL_FLAG_VARARG) == 0 ||
            (ast->call.args->num_elements < entry->fdecl->num_params)) {
          fprintf(stderr, "function %s called with %zu arguments, expected %zu\n",
                  ast->variable.ident.value.identv.ident, ast->call.args->num_elements,
                  entry->fdecl->num_params);
          ++typecheck->errors;
          return AST_TYPE_ERROR;
        }
      }

      struct ast_expr_list *args = ast->call.args;
      for (size_t i = 0; i < entry->fdecl->num_params; i++) {
        enum ast_ty arg_ty = typecheck_expr(typecheck, args->expr);
        if (arg_ty != entry->fdecl->params[i]->ty) {
          fprintf(stderr, "function %s argument %zu has type %d, expected %d\n",
                  ast->variable.ident.value.identv.ident, i + 1, arg_ty,
                  entry->fdecl->params[i]->ty);
          ++typecheck->errors;
        }

        args = args->next;
      }

      ast->ty = entry->fdecl->retty;
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_DEREF: {
      struct scope_entry *entry =
          scope_lookup(typecheck->scope, ast->deref.ident.value.identv.ident, 1);
      if (!entry || !entry->vdecl) {
        fprintf(stderr, "%s not found or not a variable\n", ast->deref.ident.value.identv.ident);
        ++typecheck->errors;
        return AST_TYPE_ERROR;
      }

      // TODO: other types of deref e.g. structs
      if (entry->vdecl->ty != AST_TYPE_FVEC) {
        fprintf(stderr, "deref %s has type %d, expected %d\n",
                ast->variable.ident.value.identv.ident, entry->vdecl->ty, AST_TYPE_FVEC);
        ++typecheck->errors;
        return AST_TYPE_ERROR;
      }

      // TODO: check # of elements from type
      ast->ty = AST_TYPE_FLOAT;
      return ast->ty;
    }; break;

    case AST_EXPR_TYPE_VOID:
      return AST_TYPE_VOID;

    default:
      fprintf(stderr, "unhandled expression type %d\n", ast->type);
  }

  // all expressions must resolve to a type
  return AST_TYPE_ERROR;
}

static int binary_mismatch_ok(int op, enum ast_ty lhs, enum ast_ty rhs) {
  // float/fvec can mix but only for certain operations
  // e.g. scale, division
  if ((lhs == AST_TYPE_FVEC && rhs == AST_TYPE_FLOAT) ||
      (lhs == AST_TYPE_FLOAT && rhs == AST_TYPE_FVEC)) {
    return op == AST_BINARY_OP_MUL || op == AST_BINARY_OP_DIV || op == AST_BINARY_OP_MOD;
  }

  return 0;
}
