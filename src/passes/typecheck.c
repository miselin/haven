#include "typecheck.h"

#include <ctype.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "kv.h"
#include "scope.h"
#include "types.h"
#include "utility.h"

struct scope_entry {
  struct ast_vdecl *vdecl;
  struct ast_fdecl *fdecl;
};

struct alias_entry {
  struct ast_ty ty;
};

struct typecheck {
  struct ast_program *ast;

  struct scope *scope;

  int errors;

  struct kv *aliases;
};

static void typecheck_ast(struct typecheck *typecheck, struct ast_program *ast);
static void typecheck_toplevel(struct typecheck *typecheck, struct ast_toplevel *ast);
static struct ast_ty typecheck_block(struct typecheck *typecheck, struct ast_block *ast);
static struct ast_ty typecheck_stmt(struct typecheck *typecheck, struct ast_stmt *ast);
static struct ast_ty typecheck_expr(struct typecheck *typecheck, struct ast_expr *ast);

static struct ast_ty resolve_type(struct typecheck *typecheck, struct ast_ty *ty);

static void typecheck_struct_decl(struct typecheck *typecheck, struct ast_ty *decl);

static int binary_mismatch_ok(int op, struct ast_ty *lhs, struct ast_ty *rhs);

static int deref_to_index(const char *deref);

__attribute__((format(printf, 3, 4))) static void typecheck_diag_expr(struct typecheck *typecheck,
                                                                      struct ast_expr *expr,
                                                                      const char *msg, ...) {
  fprintf(stderr, "%s:%zu:%zu: ", expr->loc.file, expr->loc.line, expr->loc.column);

  va_list args;
  va_start(args, msg);
  vfprintf(stderr, msg, args);
  va_end(args);

  ++typecheck->errors;
}

struct typecheck *new_typecheck(struct ast_program *ast) {
  struct typecheck *result = calloc(1, sizeof(struct typecheck));
  result->ast = ast;
  result->scope = enter_scope(NULL);
  result->aliases = new_kv();
  return result;
}

int typecheck_run(struct typecheck *typecheck) {
  typecheck_ast(typecheck, typecheck->ast);
  return typecheck->errors;
}

void destroy_typecheck(struct typecheck *typecheck) {
  exit_scope(typecheck->scope);
  void *iter = kv_iter(typecheck->aliases);
  while (!kv_end(iter)) {
    struct alias_entry *entry = kv_next(&iter);
    free(entry);
  }
  destroy_kv(typecheck->aliases);
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
  if (ast->type == AST_DECL_TYPE_FDECL) {
    ast->fdecl.retty = resolve_type(typecheck, &ast->fdecl.retty);

    struct scope_entry *existing =
        scope_lookup(typecheck->scope, ast->fdecl.ident.value.identv.ident, 1);

    struct scope_entry *entry = calloc(1, sizeof(struct scope_entry));
    entry->fdecl = &ast->fdecl;

    scope_insert(typecheck->scope, ast->fdecl.ident.value.identv.ident, entry);

    if (existing) {
      if (!same_type(&entry->fdecl->retty, &existing->fdecl->retty)) {
        char tystr[256], existingstr[256];
        type_name_into(&entry->fdecl->retty, tystr, 256);
        type_name_into(&existing->fdecl->retty, existingstr, 256);

        fprintf(stderr, "function %s redeclared with different return type %s, expected %s\n",
                ast->fdecl.ident.value.identv.ident, tystr, existingstr);
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
        entry->fdecl->params[i]->ty = resolve_type(typecheck, &entry->fdecl->params[i]->ty);

        if (!same_type(&entry->fdecl->params[i]->ty, &existing->fdecl->params[i]->ty)) {
          char tystr[256], existingstr[256];
          type_name_into(&entry->fdecl->params[i]->ty, tystr, 256);
          type_name_into(&existing->fdecl->params[i]->ty, existingstr, 256);

          fprintf(stderr, "function %s parameter %zu has type %s, expected %s\n",
                  ast->fdecl.ident.value.identv.ident, i, tystr, existingstr);
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
        struct scope_entry *param_entry = calloc(1, sizeof(struct scope_entry));
        param_entry->vdecl = ast->fdecl.params[i];
        scope_insert(typecheck->scope, ast->fdecl.params[i]->ident.value.identv.ident, param_entry);
      }

      struct ast_ty result = typecheck_block(typecheck, ast->fdecl.body);

      typecheck->scope = exit_scope(typecheck->scope);

      if (type_is_error(&result)) {
        return;
      }

      if (!same_type(&result, &ast->fdecl.retty)) {
        char resultstr[256], tystr[256];
        type_name_into(&result, resultstr, 256);
        type_name_into(&ast->fdecl.retty, tystr, 256);

        fprintf(stderr, "function %s returns %s, expected %s\n",
                ast->fdecl.ident.value.identv.ident, resultstr, tystr);
        ++typecheck->errors;
      }
    }
  } else if (ast->type == AST_DECL_TYPE_VDECL) {
    ast->vdecl.ty = resolve_type(typecheck, &ast->vdecl.ty);

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

      struct ast_ty result = typecheck_expr(typecheck, ast->vdecl.init_expr);
      if (type_is_error(&result)) {
        return;
      }

      if (!same_type(&result, &ast->vdecl.ty)) {
        char resultstr[256], tystr[256];
        type_name_into(&result, resultstr, 256);
        type_name_into(&ast->vdecl.ty, tystr, 256);

        typecheck_diag_expr(typecheck, ast->vdecl.init_expr,
                            "variable %s initializer has type %s, expected %s\n",
                            ast->vdecl.ident.value.identv.ident, resultstr, tystr);
        ++typecheck->errors;
        return;
      }
    }
  } else if (ast->type == AST_DECL_TYPE_TYDECL) {
    struct alias_entry *entry = calloc(1, sizeof(struct alias_entry));
    entry->ty = ast->tydecl.ty;
    kv_insert(typecheck->aliases, ast->tydecl.ident.value.identv.ident, entry);

    if (ast->tydecl.ty.ty == AST_TYPE_STRUCT) {
      typecheck_struct_decl(typecheck, &ast->tydecl.ty);
    }
  }
}

static void typecheck_struct_decl(struct typecheck *typecheck, struct ast_ty *decl) {
  UNUSED(typecheck);
  UNUSED(decl);

  // TODO: causes infinite recursive loop on recursive struct definitions

  struct ast_struct_field *field = decl->structty.fields;
  while (field) {
    // TODO: check for recursive definition, ensure it's a pointer if so, or it's not representable
    *field->ty = resolve_type(typecheck, field->ty);
    field = field->next;
  }
}

static struct ast_ty typecheck_block(struct typecheck *typecheck, struct ast_block *ast) {
  typecheck->scope = enter_scope(typecheck->scope);

  struct ast_stmt *stmt = ast->stmt;
  struct ast_ty last_ty;
  last_ty.ty = AST_TYPE_VOID;

  while (stmt) {
    struct ast_ty ty = typecheck_stmt(typecheck, stmt);
    if (!stmt->next) {
      last_ty = ty;
    }
    stmt = stmt->next;
  }

  typecheck->scope = exit_scope(typecheck->scope);
  return last_ty;
}

static struct ast_ty typecheck_stmt(struct typecheck *typecheck, struct ast_stmt *ast) {
  switch (ast->type) {
    case AST_STMT_TYPE_EXPR:
      return typecheck_expr(typecheck, ast->expr);

    case AST_STMT_TYPE_LET: {
      struct scope_entry *entry = calloc(1, sizeof(struct scope_entry));
      entry->vdecl = &ast->let;

      // insert before checking the initializer to allow recursive references
      scope_insert(typecheck->scope, ast->let.ident.value.identv.ident, entry);

      struct ast_ty init_ty = typecheck_expr(typecheck, ast->let.init_expr);
      if (type_is_tbd(&ast->let.ty)) {
        // inferred type
        ast->let.ty = init_ty;
      }

      if (!same_type(&ast->let.ty, &init_ty)) {
        char tystr[256], initstr[256];
        type_name_into(&ast->let.ty, tystr, 256);
        type_name_into(&init_ty, initstr, 256);

        typecheck_diag_expr(typecheck, ast->let.init_expr,
                            "let %s initializer has type %s, expected %s\n",
                            ast->let.ident.value.identv.ident, initstr, tystr);
        ++typecheck->errors;
      }
    } break;

    case AST_STMT_TYPE_ITER: {
      struct ast_ty start = typecheck_expr(typecheck, ast->iter.range.start);
      struct ast_ty end = typecheck_expr(typecheck, ast->iter.range.end);
      struct ast_ty step;
      if (ast->iter.range.step) {
        step = typecheck_expr(typecheck, ast->iter.range.step);
      } else {
        step.ty = AST_TYPE_INTEGER;
        step.integer.is_signed = 1;
        step.integer.width = 32;
      }

      if (!same_type(&start, &end)) {
        char startstr[256], endstr[256];
        type_name_into(&start, startstr, 256);
        type_name_into(&end, endstr, 256);

        fprintf(stderr, "iter range start has type %s, end has type %s\n", startstr, endstr);
        ++typecheck->errors;
      }

      if (ast->iter.range.step && !same_type(&start, &step)) {
        char startstr[256], stepstr[256];
        type_name_into(&start, startstr, 256);
        type_name_into(&step, stepstr, 256);

        fprintf(stderr, "iter range start has type %s, step has type %s\n", startstr, stepstr);
        ++typecheck->errors;
      }

      struct ast_vdecl *index = calloc(1, sizeof(struct ast_vdecl));

      struct scope_entry *entry = calloc(1, sizeof(struct scope_entry));
      entry->vdecl = index;
      entry->vdecl->ident = ast->iter.index.ident;
      entry->vdecl->ty = start;
      ast->iter.index_vdecl = entry->vdecl;

      // new scope for the loop variable
      typecheck->scope = enter_scope(typecheck->scope);

      // insert before checking the initializer to allow recursive references
      scope_insert(typecheck->scope, ast->iter.index.ident.value.identv.ident, entry);

      typecheck_block(typecheck, &ast->iter.block);

      typecheck->scope = exit_scope(typecheck->scope);
    } break;

    case AST_STMT_TYPE_STORE: {
      struct ast_ty lhs = typecheck_expr(typecheck, ast->store.lhs);
      struct ast_ty rhs = typecheck_expr(typecheck, ast->store.rhs);

      if (!(lhs.flags & TYPE_FLAG_PTR)) {
        fprintf(stderr, "store lhs is not a pointer\n");
        ++typecheck->errors;
      }

      // for type checks, remove the pointer flag
      lhs.flags &= ~TYPE_FLAG_PTR;

      if (!same_type(&lhs, &rhs)) {
        char lhsstr[256], rhsstr[256];
        type_name_into(&lhs, lhsstr, 256);
        type_name_into(&rhs, rhsstr, 256);

        fprintf(stderr, "store lhs has type %s, rhs has type %s\n", lhsstr, rhsstr);
        ++typecheck->errors;
      }

    } break;

    case AST_STMT_TYPE_RETURN: {
      // TODO: make sure this expr type matches the function's return type
      return typecheck_expr(typecheck, ast->expr);
    } break;

    default:
      fprintf(stderr, "typecheck: unhandled statement type %d\n", ast->type);
  }

  // statements that aren't expressions do not have types (their expressions do)
  return type_void();
}

static struct ast_ty typecheck_expr(struct typecheck *typecheck, struct ast_expr *ast) {
  switch (ast->type) {
    case AST_EXPR_TYPE_CONSTANT: {
      switch (ast->ty.ty) {
        case AST_TYPE_FVEC:
        case AST_TYPE_ARRAY: {
          struct ast_expr_list *node = ast->list;
          while (node) {
            typecheck_expr(typecheck, node->expr);
            node = node->next;
          }

        } break;
        default:
          break;
      }
      return resolve_type(typecheck, &ast->ty);
    } break;

    case AST_EXPR_TYPE_STRUCT_INIT: {
      // TODO: check each field initializer against the struct type
      struct ast_expr_list *node = ast->list;
      while (node) {
        typecheck_expr(typecheck, node->expr);
        node = node->next;
      }

      struct ast_ty resolved = resolve_type(typecheck, ast->ty.array.element_ty);
      *ast->ty.array.element_ty = resolved;
      return resolved;
    } break;

    case AST_EXPR_TYPE_VARIABLE: {
      struct scope_entry *entry =
          scope_lookup(typecheck->scope, ast->variable.ident.value.identv.ident, 1);
      if (!entry || !entry->vdecl) {
        fprintf(stderr, "%s not found or not a variable\n", ast->variable.ident.value.identv.ident);
        ++typecheck->errors;
        return type_error();
      }
      ast->ty = resolve_type(typecheck, &entry->vdecl->ty);
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_ARRAY_INDEX: {
      const char *ident = ast->array_index.ident.value.identv.ident;
      struct scope_entry *entry = scope_lookup(typecheck->scope, ident, 1);
      if (!entry || !entry->vdecl) {
        fprintf(stderr, "%s not found or not a variable\n", ident);
        ++typecheck->errors;
        return type_error();
      }

      if (entry->vdecl->ty.ty != AST_TYPE_ARRAY) {
        char tystr[256];
        type_name_into(&entry->vdecl->ty, tystr, 256);

        fprintf(stderr, "array index %s has type %s, expected an array type\n", ident, tystr);
        ++typecheck->errors;
        return type_error();
      }

      ast->ty = resolve_type(typecheck, entry->vdecl->ty.array.element_ty);
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_BINARY: {
      struct ast_ty lhs = typecheck_expr(typecheck, ast->binary.lhs);
      struct ast_ty rhs = typecheck_expr(typecheck, ast->binary.rhs);

      if (!same_type(&lhs, &rhs) && !binary_mismatch_ok(ast->binary.op, &lhs, &rhs)) {
        char lhsstr[256], rhsstr[256];
        type_name_into(&lhs, lhsstr, 256);
        type_name_into(&rhs, rhsstr, 256);

        fprintf(stderr, "binary op %s has mismatching lhs type %s, rhs type %s\n",
                ast_binary_op_to_str(ast->binary.op), lhsstr, rhsstr);
        ++typecheck->errors;
        return type_error();
      }

      if (ast_binary_op_conditional(ast->binary.op) || ast_binary_op_logical(ast->binary.op)) {
        // conditionals & logicals both emit 1-bit booleans
        ast->ty.ty = AST_TYPE_INTEGER;
        ast->ty.integer.is_signed = 1;
        ast->ty.integer.width = 1;
        return ast->ty;
      }

      ast->ty = resolve_type(typecheck, &lhs);
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_LOGICAL: {
      struct ast_ty lhs = typecheck_expr(typecheck, ast->logical.lhs);
      struct ast_ty rhs = typecheck_expr(typecheck, ast->logical.rhs);

      // TODO: consider widening/narrowing to make type of lhs == rhs

      if (!same_type(&lhs, &rhs)) {
        char lhsstr[256], rhsstr[256];
        type_name_into(&lhs, lhsstr, 256);
        type_name_into(&rhs, rhsstr, 256);

        fprintf(stderr, "logical op %d has mismatching lhs type %s, rhs type %s\n", ast->logical.op,
                lhsstr, rhsstr);
        ++typecheck->errors;
        return type_error();
      }

      // return type of this operation is actually a 1-bit boolean
      ast->ty.ty = AST_TYPE_INTEGER;
      ast->ty.integer.is_signed = 1;
      ast->ty.integer.width = 1;
      ast->ty = resolve_type(typecheck, &ast->ty);
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_BLOCK: {
      struct ast_ty ty = typecheck_block(typecheck, &ast->block);
      ast->ty = resolve_type(typecheck, &ty);
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_CALL: {
      struct scope_entry *entry =
          scope_lookup(typecheck->scope, ast->variable.ident.value.identv.ident, 1);
      if (!entry || !entry->fdecl) {
        fprintf(stderr, "%s not found or not a function\n", ast->variable.ident.value.identv.ident);
        ++typecheck->errors;
        return type_error();
      }

      if (!ast->call.args) {
        // no arguments passed
        if (entry->fdecl->num_params > 0) {
          fprintf(stderr, "function %s called with no arguments, expected %zu\n",
                  ast->variable.ident.value.identv.ident, entry->fdecl->num_params);
          ++typecheck->errors;
          return type_error();
        }
      } else if (entry->fdecl->num_params != ast->call.args->num_elements) {
        if ((entry->fdecl->flags & DECL_FLAG_VARARG) == 0 ||
            (ast->call.args->num_elements < entry->fdecl->num_params)) {
          fprintf(stderr, "function %s called with %zu arguments, expected %zu\n",
                  ast->variable.ident.value.identv.ident, ast->call.args->num_elements,
                  entry->fdecl->num_params);
          ++typecheck->errors;
          return type_error();
        }
      }

      struct ast_expr_list *args = ast->call.args;
      size_t i = 0;
      while (args) {
        struct ast_ty arg_ty = typecheck_expr(typecheck, args->expr);

        // check named parameters, don't check varargs (no types to check)
        if (i < entry->fdecl->num_params) {
          if (!same_type(&arg_ty, &entry->fdecl->params[i]->ty)) {
            char tystr[256], expectedstr[256];
            type_name_into(&arg_ty, tystr, 256);
            type_name_into(&entry->fdecl->params[i]->ty, expectedstr, 256);

            fprintf(stderr, "function %s argument %zu has type %s, expected %s\n",
                    ast->variable.ident.value.identv.ident, i + 1, tystr, expectedstr);
            ++typecheck->errors;
          }
        }

        args = args->next;
        ++i;
      }

      ast->ty = resolve_type(typecheck, &entry->fdecl->retty);
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_DEREF: {
      struct scope_entry *entry =
          scope_lookup(typecheck->scope, ast->deref.ident.value.identv.ident, 1);
      if (!entry || !entry->vdecl) {
        fprintf(stderr, "%s not found or not a variable\n", ast->deref.ident.value.identv.ident);
        ++typecheck->errors;
        return type_error();
      }

      if (entry->vdecl->ty.ty != AST_TYPE_FVEC && entry->vdecl->ty.ty != AST_TYPE_STRUCT) {
        char tystr[256];
        type_name_into(&entry->vdecl->ty, tystr, 256);

        fprintf(stderr, "deref %s has type %s, expected a vector or struct type\n",
                ast->variable.ident.value.identv.ident, tystr);
        ++typecheck->errors;
        return type_error();
      }

      size_t max_field = 0;

      if (entry->vdecl->ty.ty == AST_TYPE_FVEC) {
        int deref = deref_to_index(ast->deref.field.value.identv.ident);
        if (deref < 0) {
          fprintf(stderr, "fvec deref %s has unknown field %s\n",
                  ast->variable.ident.value.identv.ident, ast->deref.field.value.identv.ident);
          ++typecheck->errors;
          return type_error();
        }
        ast->deref.field_idx = (size_t)deref;
        max_field = entry->vdecl->ty.fvec.width;

        ast->ty.ty = AST_TYPE_FLOAT;
      } else if (entry->vdecl->ty.ty == AST_TYPE_STRUCT) {
        struct ast_struct_field *field = entry->vdecl->ty.structty.fields;
        while (field) {
          if (strcmp(field->name, ast->deref.field.value.identv.ident) == 0) {
            ast->deref.field_idx = max_field;
            break;
          }
          field = field->next;
        }

        if (!field) {
          fprintf(stderr, "struct deref %s references unknown field %s\n",
                  ast->variable.ident.value.identv.ident, ast->deref.field.value.identv.ident);
          ++typecheck->errors;
          return type_error();
        }

        ast->ty = *field->ty;
        max_field = entry->vdecl->ty.structty.num_fields;
      }

      // can't deref past the width of the vector
      if (ast->deref.field_idx >= max_field) {
        fprintf(stderr, "deref %s has field #%zd, exceeding field count of %zd\n",
                ast->variable.ident.value.identv.ident, ast->deref.field_idx, max_field);
        ++typecheck->errors;
        return type_error();
      }

      ast->ty = resolve_type(typecheck, &ast->ty);
      return ast->ty;
    }; break;

    case AST_EXPR_TYPE_VOID:
      return type_void();

    case AST_EXPR_TYPE_CAST: {
      ast->cast.ty = resolve_type(typecheck, &ast->cast.ty);

      struct ast_ty expr_ty = typecheck_expr(typecheck, ast->cast.expr);
      if (type_is_error(&expr_ty)) {
        return type_error();
      }

      if (!can_cast(&ast->cast.ty, &expr_ty)) {
        char tystr[256], exprstr[256];
        type_name_into(&ast->cast.ty, tystr, 256);
        type_name_into(&expr_ty, exprstr, 256);

        fprintf(stderr, "incompatible cast from %s to %s\n", exprstr, tystr);
        ++typecheck->errors;
        return type_error();
      }

      ast->ty = resolve_type(typecheck, &ast->cast.ty);
      return ast->ty;

    } break;

    case AST_EXPR_TYPE_IF: {
      struct ast_ty cond = typecheck_expr(typecheck, ast->if_expr.cond);
      if (type_is_error(&cond)) {
        return type_error();
      }

      struct ast_ty then_ty = typecheck_block(typecheck, &ast->if_expr.then_block);
      if (type_is_error(&then_ty)) {
        return type_error();
      }

      if (ast->if_expr.has_else) {
        struct ast_ty else_ty = typecheck_block(typecheck, &ast->if_expr.else_block);
        if (type_is_error(&else_ty)) {
          return type_error();
        }

        if (!same_type(&then_ty, &else_ty)) {
          char thenstr[256], elsestr[256];
          type_name_into(&then_ty, thenstr, 256);
          type_name_into(&else_ty, elsestr, 256);

          fprintf(stderr, "if then block has type %s, else block has type %s\n", thenstr, elsestr);
          ++typecheck->errors;
          return type_error();
        }

        ast->ty = then_ty;
        return ast->ty;
      }

      if (then_ty.ty != AST_TYPE_VOID && !ast->if_expr.has_else) {
        fprintf(stderr, "an else block is required when if is used as an expression\n");
        ++typecheck->errors;
        return type_error();
      }

      ast->ty = resolve_type(typecheck, &then_ty);
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_ASSIGN: {
      const char *ident = ast->assign.ident.value.identv.ident;
      struct scope_entry *entry = scope_lookup(typecheck->scope, ident, 1);
      if (!entry || !entry->vdecl) {
        fprintf(stderr, "%s not found or not a variable\n", ident);
        ++typecheck->errors;
        return type_error();
      }

      if (!(entry->vdecl->flags & DECL_FLAG_MUT)) {
        fprintf(stderr, "%s is not mutable\n", ident);
        ++typecheck->errors;
        return type_error();
      }

      struct ast_ty expr_ty = typecheck_expr(typecheck, ast->assign.expr);
      if (type_is_error(&expr_ty)) {
        return type_error();
      }

      if (type_is_tbd(&entry->vdecl->ty)) {
        // inferred type
        entry->vdecl->ty = expr_ty;
      }

      if (!same_type(&entry->vdecl->ty, &expr_ty)) {
        char tystr[256], exprstr[256];
        type_name_into(&entry->vdecl->ty, tystr, 256);
        type_name_into(&expr_ty, exprstr, 256);

        fprintf(stderr, "assignment to %s has type %s, expected %s\n", ident, exprstr, tystr);
        ++typecheck->errors;
        return type_error();
      }

      ast->ty = resolve_type(typecheck, &expr_ty);
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_REF: {
      struct ast_expr *expr = ast->ref.expr;

      if (expr->type != AST_EXPR_TYPE_VARIABLE) {
        fprintf(stderr, "ref expression must resolve to an identifier\n");
        ++typecheck->errors;
        return type_error();
      }

      const char *ident = expr->variable.ident.value.identv.ident;

      struct scope_entry *entry = scope_lookup(typecheck->scope, ident, 1);
      if (!entry || !entry->vdecl) {
        fprintf(stderr, "%s not found or not a variable\n", ident);
        ++typecheck->errors;
        return type_error();
      }

      ast->ty = entry->vdecl->ty;
      ast->ty.flags |= TYPE_FLAG_PTR;
      ast->ref.expr->ty.flags |= TYPE_FLAG_PTR;
      ast->ty = resolve_type(typecheck, &ast->ty);
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_LOAD: {
      struct ast_expr *expr = ast->load.expr;

      struct ast_ty expr_ty = typecheck_expr(typecheck, expr);
      if (type_is_error(&expr_ty)) {
        return type_error();
      }

      if (!(expr_ty.flags & TYPE_FLAG_PTR)) {
        fprintf(stderr, "load expression must resolve to a pointer\n");
        ++typecheck->errors;
        return type_error();
      }

      ast->ty = expr_ty;
      ast->ty.flags &= ~TYPE_FLAG_PTR;
      ast->ty = resolve_type(typecheck, &ast->ty);
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_UNARY: {
      struct ast_ty expr_ty = typecheck_expr(typecheck, ast->unary.expr);
      if (type_is_error(&expr_ty)) {
        return type_error();
      }

      switch (ast->unary.op) {
        case AST_UNARY_OP_NEG:
          if (expr_ty.ty != AST_TYPE_INTEGER && expr_ty.ty != AST_TYPE_FLOAT) {
            fprintf(stderr, "negation expression must resolve to an integer or float\n");
            ++typecheck->errors;
            return type_error();
          }

          ast->ty = resolve_type(typecheck, &expr_ty);
          return ast->ty;

        case AST_UNARY_OP_NOT:
          if (expr_ty.ty != AST_TYPE_INTEGER) {
            fprintf(stderr, "not expression must resolve to an integer\n");
            ++typecheck->errors;
            return type_error();
          }

          ast->ty = resolve_type(typecheck, &expr_ty);
          return ast->ty;

        case AST_UNARY_OP_COMP:
          if (expr_ty.ty != AST_TYPE_INTEGER) {
            fprintf(stderr, "complement expression must resolve to an integer\n");
            ++typecheck->errors;
            return type_error();
          }

          ast->ty = resolve_type(typecheck, &expr_ty);
          return ast->ty;

        default:
          fprintf(stderr, "unhandled unary op %d\n", ast->unary.op);
          ++typecheck->errors;
          return type_error();
      }
    } break;

    case AST_EXPR_TYPE_BOOLEAN: {
      typecheck_expr(typecheck, ast->boolean.lhs);
      typecheck_expr(typecheck, ast->boolean.rhs);

      ast->ty.ty = AST_TYPE_INTEGER;
      ast->ty.integer.is_signed = 1;
      ast->ty.integer.width = 1;
      ast->ty = resolve_type(typecheck, &ast->ty);
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_MATCH: {
      struct ast_ty expr_ty = typecheck_expr(typecheck, ast->match.expr);
      if (type_is_error(&expr_ty)) {
        fprintf(stderr, "match expression has type error\n");
        return type_error();
      }

      // first pass: check that all arms have the same pattern type, and check their expressions
      struct ast_expr_match_arm *arm = ast->match.arms;
      while (arm) {
        struct ast_ty pattern_ty = typecheck_expr(typecheck, arm->pattern);
        if (type_is_error(&pattern_ty)) {
          fprintf(stderr, "match arm pattern has type error\n");
          return type_error();
        }

        if (!same_type(&pattern_ty, &expr_ty)) {
          char wantstr[256], gotstr[256];
          type_name_into(&pattern_ty, wantstr, 256);
          type_name_into(&expr_ty, gotstr, 256);

          fprintf(stderr, "match patterns has incorrect type, wanted %s but got %s\n", wantstr,
                  gotstr);
          ++typecheck->errors;
          return type_error();
        }

        struct ast_ty arm_ty = typecheck_expr(typecheck, arm->expr);
        if (type_is_error(&arm_ty)) {
          fprintf(stderr, "match arm expression has type error\n");
          return type_error();
        }

        arm = arm->next;
      }

      if (!ast->match.otherwise) {
        fprintf(stderr, "match expression has no otherwise arm\n");
        ++typecheck->errors;
        return type_error();
      }

      struct ast_ty otherwise_ty = typecheck_expr(typecheck, ast->match.otherwise->expr);
      if (type_is_error(&otherwise_ty)) {
        fprintf(stderr, "match otherwise arm has type error\n");
        return type_error();
      }

      // second pass: check that all arms have the same type
      arm = ast->match.arms;
      while (arm) {
        struct ast_expr_match_arm *next = arm->next ? arm->next : ast->match.otherwise;
        if (next) {
          if (!same_type(&arm->expr->ty, &next->expr->ty)) {
            char armstr[256], nextstr[256];
            type_name_into(&arm->expr->ty, armstr, 256);
            type_name_into(&next->expr->ty, nextstr, 256);

            fprintf(stderr, "match arm has type %s, next arm has mismatched type %s\n", armstr,
                    nextstr);
            ++typecheck->errors;
            return type_error();
          }
        }

        arm = arm->next;
      }

      ast->ty = resolve_type(typecheck, &ast->match.arms->expr->ty);
      return ast->ty;
    } break;

    default:
      fprintf(stderr, "typecheck: unhandled expression type %d\n", ast->type);
  }

  // all expressions must resolve to a type
  return type_error();
}

static int binary_mismatch_ok(int op, struct ast_ty *lhs, struct ast_ty *rhs) {
  // float/fvec can mix but only for certain operations
  // e.g. scale, division
  if ((lhs->ty == AST_TYPE_FVEC && rhs->ty == AST_TYPE_FLOAT) ||
      (lhs->ty == AST_TYPE_FLOAT && rhs->ty == AST_TYPE_FVEC)) {
    return op == AST_BINARY_OP_MUL || op == AST_BINARY_OP_DIV || op == AST_BINARY_OP_MOD;
  }

  return 0;
}

static struct ast_ty resolve_type(struct typecheck *typecheck, struct ast_ty *ty) {
  if (ty->ty != AST_TYPE_CUSTOM) {
    return *ty;
  }

  struct alias_entry *entry = kv_lookup(typecheck->aliases, ty->name);
  if (!entry) {
    return type_error();
  }

  // copy flags from original type (e.g. ptr)
  entry->ty.flags |= ty->flags;
  entry->ty.flags |= TYPE_FLAG_INDIRECT;
  return entry->ty;
}

static int deref_to_index(const char *deref) {
  if (isdigit(*deref)) {
    // numeric deref
    // TODO: check that endptr is the end of deref; fully consume the string
    return (int)strtol(deref, NULL, 10);
  }

  if (deref[1] == 0) {
    // single character deref
    switch (deref[0]) {
      // XYZW
      case 'x':
      case 'y':
      case 'z':
        return deref[0] - 'x';
      case 'w':
        return 3;

      // RGBA
      case 'r':
        return 0;
      case 'g':
        return 1;
      case 'b':
        return 2;
      case 'a':
        return 3;
    }
  }

  return -1;
}
