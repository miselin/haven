#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "compiler.h"
#include "internal.h"
#include "kv.h"
#include "scope.h"
#include "typecheck.h"
#include "types.h"

struct ast_ty *typecheck_expr(struct typecheck *typecheck, struct ast_expr *ast) {
  struct ast_ty *ty = typecheck_expr_with_tbds(typecheck, ast);
  if (!ty) {
    return ty;
  }

  if (type_is_tbd(ty)) {
    typecheck_diag_expr(typecheck, ast,
                        "type is not yet resolved, but this context requires a resolved type\n");
    return NULL;
  }

  return ty;
}

struct ast_ty *typecheck_expr_with_tbds(struct typecheck *typecheck, struct ast_expr *ast) {
  struct ast_ty *ty = typecheck_expr_inner(typecheck, ast);
  if (!ty) {
    return ty;
  }

  if (type_is_error(ty)) {
    typecheck_diag_expr(typecheck, ast, "type failed to resolve\n");
    return NULL;
  }

  return ty;
}

struct ast_ty *typecheck_expr_inner(struct typecheck *typecheck, struct ast_expr *ast) {
  switch (ast->type) {
    case AST_EXPR_TYPE_CONSTANT: {
      struct ast_ty new_ty = resolve_type(typecheck, &ast->ty);
      free_ty(&ast->ty, 0);
      ast->ty = new_ty;

      switch (ast->ty.ty) {
        case AST_TYPE_FVEC:
        case AST_TYPE_ARRAY:
        case AST_TYPE_MATRIX: {
          struct ast_expr_list *node = ast->list;
          while (node) {
            struct ast_ty *ty = typecheck_expr(typecheck, node->expr);
            if (!ty) {
              return NULL;
            }

            if (ast->ty.ty == AST_TYPE_ARRAY) {
              if (ast->ty.array.element_ty->ty == AST_TYPE_MATRIX) {
                // swap to matrix type after checking the inners are actually fvecs
                if (ty->ty != AST_TYPE_FVEC) {
                  typecheck_diag_expr(typecheck, node->expr,
                                      "matrix initializer has non-fvec type\n");
                  return NULL;
                }
              }

              maybe_implicitly_convert(ty, ast->ty.array.element_ty);
            }

            node = node->next;
          }

          // convert from array to full matrix type
          if (ast->ty.ty == AST_TYPE_ARRAY) {
            if (ast->ty.array.element_ty->ty == AST_TYPE_MATRIX) {
              size_t correct_cols = ast->ty.array.element_ty->matrix.cols;
              size_t correct_rows = ast->ty.array.element_ty->matrix.rows;

              size_t rows = ast->list->num_elements;
              size_t cols = ast->list->expr->list->num_elements;
              free_ty(ast->ty.array.element_ty, 1);

              ast->ty.ty = AST_TYPE_MATRIX;
              ast->ty.matrix.cols = cols;
              ast->ty.matrix.rows = rows;

              if (cols != correct_cols) {
                typecheck_diag_expr(typecheck, ast,
                                    "matrix initializer has %zu columns, expected %zu\n", cols,
                                    correct_cols);
                return NULL;
              }
              if (rows != correct_rows) {
                typecheck_diag_expr(typecheck, ast,
                                    "matrix initializer has %zu rows, expected %zu\n", rows,
                                    correct_rows);
                return NULL;
              }
            }
          }
        } break;
        default:
          break;
      }
      return &ast->ty;
    } break;

    case AST_EXPR_TYPE_STRUCT_INIT: {
      struct ast_ty resolved = resolve_type(typecheck, ast->ty.array.element_ty);

      struct ast_struct_field *field = resolved.structty.fields;
      struct ast_expr_list *node = ast->list;
      while (node) {
        // TODO: fuzzer found field to be null here, the AST doesn't make sense to cause that
        // either way though, we should check that both node & field are non-null
        if (!field) {
          fprintf(stderr, "struct initializer has more fields than the struct type\n");
          ++typecheck->errors;
          break;
        }

        if (!field->ty) {
          fprintf(stderr, "struct initializer field %s inexplicably has no type\n", field->name);
          ++typecheck->errors;
        }

        struct ast_ty *expr_ty = typecheck_expr(typecheck, node->expr);
        if (!expr_ty) {
          return NULL;
        }

        maybe_implicitly_convert(expr_ty, field->ty);

        if (!same_type(expr_ty, field->ty)) {
          char exprty[256];
          char fieldty[256];
          type_name_into(expr_ty, exprty, 256);
          type_name_into(field->ty, fieldty, 256);

          fprintf(stderr, "struct initializer field %s has type %s, expected %s\n", field->name,
                  exprty, fieldty);
          ++typecheck->errors;
        }

        node = node->next;
        field = field->next;
      }

      // original got copied when we resolved
      *ast->ty.array.element_ty = resolved;
      return ast->ty.array.element_ty;
    } break;

    case AST_EXPR_TYPE_UNION_INIT: {
      // unions are a simplified variant of structs - they initialize one field, and that's it
      struct ast_ty union_ty = resolve_type(typecheck, &ast->union_init.ty);
      free_ty(&ast->union_init.ty, 0);
      ast->union_init.ty = union_ty;

      if (type_is_error(&union_ty)) {
        typecheck_diag_expr(typecheck, ast, "union type could not be resolved\n");
        return &typecheck->error_type;
      }

      // find the field
      struct ast_struct_field *field = union_ty.structty.fields;
      while (field) {
        if (strcmp(field->name, ast->union_init.field.value.identv.ident) == 0) {
          break;
        }
        field = field->next;
      }

      if (!field) {
        typecheck_diag_expr(typecheck, ast, "union field %s not found\n",
                            ast->union_init.field.value.identv.ident);
        return &typecheck->error_type;
      }

      struct ast_ty *expr_ty = typecheck_expr(typecheck, ast->union_init.inner);
      if (!expr_ty) {
        return NULL;
      }

      maybe_implicitly_convert(expr_ty, field->ty);

      if (!same_type(expr_ty, field->ty)) {
        char exprty[256];
        char fieldty[256];
        type_name_into(expr_ty, exprty, 256);
        type_name_into(field->ty, fieldty, 256);

        fprintf(stderr, "union initializer field %s has type %s, expected %s\n",
                ast->union_init.field.value.identv.ident, exprty, fieldty);
        ++typecheck->errors;
      }

      ast->ty = resolve_type(typecheck, &union_ty);
      return &ast->ty;
    } break;

    case AST_EXPR_TYPE_VARIABLE: {
      struct scope_entry *entry =
          scope_lookup(typecheck->scope, ast->variable.ident.value.identv.ident, 1);
      if (!entry || !entry->vdecl) {
        fprintf(stderr, "%s not found or not a variable\n", ast->variable.ident.value.identv.ident);
        ++typecheck->errors;
        return &typecheck->error_type;
      }
      struct ast_ty resolved = resolve_type(typecheck, &entry->vdecl->ty);
      free_ty(&ast->ty, 0);
      ast->ty = resolved;
      return &ast->ty;
    } break;

    case AST_EXPR_TYPE_ARRAY_INDEX: {
      struct ast_ty *target_ty = typecheck_expr(typecheck, ast->array_index.target);
      if (!target_ty) {
        return NULL;
      }

      const char *ident = ast_expr_ident(ast->array_index.target);

      // is the target indexable?
      if (!type_is_indexable(target_ty)) {
        typecheck_diag_expr(typecheck, ast->array_index.target, "%s is not indexable\n", ident);
        ++typecheck->errors;
        return &typecheck->error_type;
      }

      // try to coerce the index to a 32-bit integer
      struct ast_ty *index_ty = typecheck_expr(typecheck, ast->array_index.index);
      if (!index_ty) {
        return NULL;
      }

      struct ast_ty int_ty;
      int_ty.ty = AST_TYPE_INTEGER;
      int_ty.integer.is_signed = 1;
      int_ty.integer.width = 32;
      maybe_implicitly_convert(index_ty, &int_ty);

      struct ast_expr *target_expr = ast->array_index.target;

      if (target_expr->ty.ty == AST_TYPE_ARRAY) {
        ast->ty = resolve_type(typecheck, target_expr->ty.array.element_ty);
      } else if (target_expr->ty.ty == AST_TYPE_POINTER) {
        // type of expression is the type pointed to by the pointer
        ast->ty = resolve_type(typecheck, ptr_pointee_type(&target_expr->ty));
      } else if (target_expr->ty.ty == AST_TYPE_BOX) {
        // type of expression is the type pointed to by the box
        ast->ty = resolve_type(typecheck, box_pointee_type(&target_expr->ty));
      } else {
        typecheck_diag_expr(typecheck, ast->array_index.target,
                            "indexable type has an unimplemented type resolve\n");
        ++typecheck->errors;
        return &typecheck->error_type;
      }
      return &ast->ty;
    } break;

    case AST_EXPR_TYPE_BINARY: {
      struct ast_ty *lhs = typecheck_expr(typecheck, ast->binary.lhs);
      struct ast_ty *rhs = typecheck_expr(typecheck, ast->binary.rhs);

      if (!lhs || !rhs) {
        return NULL;
      }

      maybe_implicitly_convert(lhs, rhs);
      maybe_implicitly_convert(rhs, lhs);

      if (!same_type(lhs, rhs) && !binary_mismatch_ok(ast->binary.op, lhs, rhs)) {
        char lhsstr[256], rhsstr[256];
        type_name_into(lhs, lhsstr, 256);
        type_name_into(rhs, rhsstr, 256);

        typecheck_diag_expr(typecheck, ast,
                            "binary op %s has mismatching lhs type %s, rhs type %s\n",
                            ast_binary_op_to_str(ast->binary.op), lhsstr, rhsstr);
        ++typecheck->errors;
        return &typecheck->error_type;
      }

      if (ast_binary_op_conditional(ast->binary.op) || ast_binary_op_logical(ast->binary.op)) {
        // conditionals & logicals both emit 1-bit booleans
        // don't set signed flag - booleans need to zero-extend in conversions, not sign-extend
        ast->ty.ty = AST_TYPE_INTEGER;
        ast->ty.integer.is_signed = 0;
        ast->ty.integer.width = 1;
        return &ast->ty;
      }

      ast->ty = resolve_type(typecheck, lhs);
      return &ast->ty;
    } break;

    case AST_EXPR_TYPE_BLOCK: {
      struct ast_ty *ty = typecheck_block(typecheck, &ast->block);
      ast->ty = resolve_type(typecheck, ty);
      return &ast->ty;
    } break;

    case AST_EXPR_TYPE_CALL: {
      struct scope_entry *entry =
          scope_lookup(typecheck->scope, ast->variable.ident.value.identv.ident, 1);
      if (!entry || !entry->fdecl) {
        fprintf(stderr, "%s not found or not a function\n", ast->variable.ident.value.identv.ident);
        ++typecheck->errors;
        return &typecheck->error_type;
      }

      if (!ast->call.args) {
        // no arguments passed
        if (entry->fdecl->num_params > 0) {
          fprintf(stderr, "function %s called with no arguments, expected %zu\n",
                  ast->variable.ident.value.identv.ident, entry->fdecl->num_params);
          ++typecheck->errors;
          return &typecheck->error_type;
          ;
        }
      } else if (entry->fdecl->num_params != ast->call.args->num_elements) {
        if ((entry->fdecl->flags & DECL_FLAG_VARARG) == 0 ||
            (ast->call.args->num_elements < entry->fdecl->num_params)) {
          fprintf(stderr, "function %s called with %zu arguments, expected %zu\n",
                  ast->variable.ident.value.identv.ident, ast->call.args->num_elements,
                  entry->fdecl->num_params);
          ++typecheck->errors;
          return &typecheck->error_type;
          ;
        }
      }

      ast->call.fdecl = entry->fdecl;

      struct ast_expr_list *args = ast->call.args;
      size_t i = 0;
      while (args) {
        struct ast_ty *arg_ty = typecheck_expr(typecheck, args->expr);
        if (!arg_ty) {
          return NULL;
        }

        // check named parameters, don't check varargs (no types to check)
        if (i < entry->fdecl->num_params) {
          maybe_implicitly_convert(arg_ty, &entry->fdecl->params[i]->ty);
          if (!same_type(arg_ty, &entry->fdecl->params[i]->ty)) {
            char tystr[256], expectedstr[256];
            type_name_into(arg_ty, tystr, 256);
            type_name_into(&entry->fdecl->params[i]->ty, expectedstr, 256);

            fprintf(stderr, "function %s argument %zu has type %s, expected %s\n",
                    ast->variable.ident.value.identv.ident, i + 1, tystr, expectedstr);
            ++typecheck->errors;
          }
        }

        args = args->next;
        ++i;
      }

      struct ast_ty resolved = resolve_type(typecheck, &entry->fdecl->retty);
      free_ty(&ast->ty, 0);
      ast->ty = resolved;
      return &ast->ty;
    } break;

    case AST_EXPR_TYPE_DEREF: {
      struct ast_ty *target_ty = typecheck_expr(typecheck, ast->deref.target);
      if (!target_ty) {
        return NULL;
      }

      const char *ident = ast_expr_ident(ast);

      struct ast_ty *ty = target_ty;
      if (ast->deref.is_ptr) {
        if (ty->ty == AST_TYPE_POINTER) {
          ty = ptr_pointee_type(ty);
        } else if (ty->ty == AST_TYPE_BOX) {
          ty = box_pointee_type(ty);
        } else {
          fprintf(stderr, "deref of %s has non-pointer type\n", ident);
          ++typecheck->errors;
          return &typecheck->error_type;
        }
      }

      if (ty->ty != AST_TYPE_FVEC && ty->ty != AST_TYPE_STRUCT && ty->ty != AST_TYPE_MATRIX) {
        char tystr[256];
        type_name_into(ty, tystr, 256);

        fprintf(stderr, "deref of %s has type %s, expected a vector or struct type\n", ident,
                tystr);
        ++typecheck->errors;
        return &typecheck->error_type;
      }

      size_t max_field = 0;

      if (ty->ty == AST_TYPE_FVEC) {
        int deref = deref_to_index(ast->deref.field.value.identv.ident);
        if (deref < 0) {
          fprintf(stderr, "fvec deref %s has unknown field %s\n", ident,
                  ast->deref.field.value.identv.ident);
          ++typecheck->errors;
          return &typecheck->error_type;
          ;
        }
        ast->deref.field_idx = (size_t)deref;
        max_field = ty->fvec.width;

        ast->ty.ty = AST_TYPE_FLOAT;
      } else if (ty->ty == AST_TYPE_STRUCT) {
        struct ast_struct_field *field = ty->structty.fields;
        size_t i = 0;
        while (field) {
          if (strcmp(field->name, ast->deref.field.value.identv.ident) == 0) {
            ast->deref.field_idx = i;
            break;
          }
          field = field->next;
          ++i;
        }

        if (!field) {
          fprintf(stderr, "struct deref %s references unknown field %s\n", ident,
                  ast->deref.field.value.identv.ident);
          ++typecheck->errors;
          return &typecheck->error_type;
          ;
        }

        ast->ty = *field->ty;
        max_field = ty->structty.num_fields;
      } else if (ty->ty == AST_TYPE_MATRIX) {
        int deref = deref_to_index(ast->deref.field.value.identv.ident);
        if (deref < 0) {
          fprintf(stderr, "matrix deref %s has unknown field %s\n", ident,
                  ast->deref.field.value.identv.ident);
          ++typecheck->errors;
          return &typecheck->error_type;
          ;
        }
        ast->deref.field_idx = (size_t)deref;
        max_field = ty->matrix.rows;

        ast->ty.ty = AST_TYPE_FVEC;
        ast->ty.fvec.width = ty->matrix.cols;
      }

      // can't deref past the width of the vector
      if (ast->deref.field_idx >= max_field) {
        fprintf(stderr, "deref %s has field #%zd, exceeding field count of %zd\n", ident,
                ast->deref.field_idx, max_field);
        ++typecheck->errors;
        return &typecheck->error_type;
      }

      ast->ty = resolve_type(typecheck, &ast->ty);
      return &ast->ty;
    }; break;

    case AST_EXPR_TYPE_VOID:
      ast->ty = type_void();
      return &ast->ty;

    case AST_EXPR_TYPE_CAST: {
      struct ast_ty resolved = resolve_type(typecheck, &ast->cast.ty);
      free_ty(&ast->cast.ty, 0);
      ast->cast.ty = resolved;

      struct ast_ty *expr_ty = typecheck_expr(typecheck, ast->cast.expr);
      if (!expr_ty) {
        return NULL;
      }

      if (!can_cast(&ast->cast.ty, expr_ty)) {
        char tystr[256], exprstr[256];
        type_name_into(&ast->cast.ty, tystr, 256);
        type_name_into(expr_ty, exprstr, 256);

        fprintf(stderr, "incompatible cast from %s to %s\n", exprstr, tystr);
        ++typecheck->errors;
        return &typecheck->error_type;
      }

      resolved = resolve_type(typecheck, &ast->cast.ty);
      free_ty(&ast->ty, 0);
      ast->ty = resolved;
      return &ast->ty;
    } break;

    case AST_EXPR_TYPE_IF: {
      struct ast_ty *cond = typecheck_expr(typecheck, ast->if_expr.cond);
      if (!cond) {
        return NULL;
      }

      struct ast_ty *then_ty = typecheck_block(typecheck, &ast->if_expr.then_block);
      if (!then_ty) {
        return NULL;
      }

      if (ast->if_expr.elseifs) {
        struct ast_expr_elseif *elseif = ast->if_expr.elseifs;
        while (elseif) {
          struct ast_ty *cond_ty = typecheck_expr(typecheck, elseif->cond);
          if (!cond_ty) {
            return NULL;
          }

          struct ast_ty *block_ty = typecheck_block(typecheck, &elseif->block);
          if (!block_ty) {
            return NULL;
          }

          maybe_implicitly_convert(block_ty, then_ty);

          if (!same_type(then_ty, block_ty)) {
            char thenstr[256], blockstr[256];
            type_name_into(then_ty, thenstr, 256);
            type_name_into(block_ty, blockstr, 256);

            fprintf(stderr, "elseif block has type %s, expected %s\n", blockstr, thenstr);
            ++typecheck->errors;
            return &typecheck->error_type;
          }

          elseif = elseif->next;
        }
      }

      if (ast->if_expr.has_else) {
        struct ast_ty *else_ty = typecheck_block(typecheck, &ast->if_expr.else_block);
        if (!else_ty) {
          return NULL;
        }

        maybe_implicitly_convert(else_ty, then_ty);

        if (!same_type(then_ty, else_ty)) {
          char thenstr[256], elsestr[256];
          type_name_into(then_ty, thenstr, 256);
          type_name_into(else_ty, elsestr, 256);

          fprintf(stderr, "if then block has type %s, else block has type %s\n", thenstr, elsestr);
          ++typecheck->errors;
          return &typecheck->error_type;
        }
      }

      if (then_ty->ty != AST_TYPE_VOID && !ast->if_expr.has_else) {
        fprintf(stderr, "an else block is required when if is used as an expression\n");
        ++typecheck->errors;
        return &typecheck->error_type;
      }

      struct ast_ty resolved = resolve_type(typecheck, then_ty);
      free_ty(&ast->ty, 0);
      ast->ty = resolved;
      return &ast->ty;
    } break;

    case AST_EXPR_TYPE_ASSIGN: {
      struct ast_ty *lhs_ty = typecheck_expr(typecheck, ast->assign.lhs);
      if (!lhs_ty) {
        return NULL;
      }

      struct ast_ty *expr_ty = typecheck_expr_with_tbds(typecheck, ast->assign.expr);
      if (!expr_ty) {
        return NULL;
      }

      const char *ident = ast_expr_ident(ast->assign.lhs);

      {
        if (!ident) {
          compiler_log(typecheck->compiler, LogLevelDebug, "typecheck", "assign lhs not an ident");
          return NULL;  // TODO: this should happen in semantic pass not here
        }

        struct scope_entry *entry = scope_lookup(typecheck->scope, ident, 1);
        if (!entry || !entry->vdecl) {
          fprintf(stderr, "in assignment, %s not found or not a variable\n", ident);
          ++typecheck->errors;
          return &typecheck->error_type;
        }

        if (!(entry->vdecl->flags & DECL_FLAG_MUT)) {
          fprintf(stderr, "%s is not mutable\n", ident);
          ++typecheck->errors;
          return &typecheck->error_type;
        }

        if (type_is_tbd(&entry->vdecl->ty)) {
          // inferred type
          entry->vdecl->ty = *expr_ty;

          // remove constant flag if it was inferred
          entry->vdecl->ty.flags &= ~TYPE_FLAG_CONSTANT;
        }
      }

      struct ast_ty *desired_ty = lhs_ty;

      const char *field_name = NULL;

      if (ast->assign.lhs->type == AST_EXPR_TYPE_DEREF) {
        // all the checks in this block are probably already handled fine by DEREF, but let's make
        // sure

        struct ast_ty *deref_ty = &ast->assign.lhs->deref.target->ty;
        if (ast->assign.lhs->deref.is_ptr) {
          if (deref_ty->ty == AST_TYPE_POINTER) {
            deref_ty = ptr_pointee_type(deref_ty);
          } else if (deref_ty->ty == AST_TYPE_BOX) {
            deref_ty = box_pointee_type(deref_ty);
          }

          if (!deref_ty) {
            fprintf(stderr, "dereference of non-pointer type\n");
            ++typecheck->errors;
            return &typecheck->error_type;
          }
        }

        if (deref_ty->ty != AST_TYPE_STRUCT) {
          char tyname[256];
          type_name_into(lhs_ty, tyname, 256);
          fprintf(stderr, "in field assignment, lhs %s has non-struct type %s\n", ident, tyname);
          ++typecheck->errors;
          return &typecheck->error_type;
        }

        struct ast_struct_field *field = deref_ty->structty.fields;
        while (field) {
          if (strcmp(field->name, ast->assign.lhs->deref.field.value.identv.ident) == 0) {
            field_name = field->name;
            desired_ty = field->ty;
            break;
          }
          field = field->next;
        }

        if (!field) {
          fprintf(stderr, "field %s not found in struct %s\n",
                  ast->assign.lhs->deref.field.value.identv.ident, ident);
          ++typecheck->errors;
          return &typecheck->error_type;
        }
      }

      maybe_implicitly_convert(expr_ty, desired_ty);

      if (!same_type(desired_ty, expr_ty)) {
        char tystr[256], exprstr[256];
        type_name_into(desired_ty, tystr, 256);
        type_name_into(expr_ty, exprstr, 256);

        if (field_name) {
          fprintf(stderr, "field assignment to %s.%s has type %s, expected %s\n", ident, field_name,
                  exprstr, tystr);
        } else {
          fprintf(stderr, "assignment to %s has type %s, expected %s\n", ident, exprstr, tystr);
        }
        ++typecheck->errors;
        return &typecheck->error_type;
      }

      ast->ty = resolve_type(typecheck, expr_ty);
      return &ast->ty;
    } break;

    case AST_EXPR_TYPE_REF: {
      compiler_log(typecheck->compiler, LogLevelDebug, "typecheck", "typechecking ref");

      struct ast_expr *expr = ast->ref.expr;

      struct ast_ty *expr_ty = typecheck_expr(typecheck, expr);
      if (!expr_ty) {
        return NULL;
      }

      if (expr->type != AST_EXPR_TYPE_VARIABLE) {
        fprintf(stderr, "ref expression must resolve to an identifier\n");
        ++typecheck->errors;
        return &typecheck->error_type;
      }

      const char *ident = expr->variable.ident.value.identv.ident;

      struct scope_entry *entry = scope_lookup(typecheck->scope, ident, 1);
      if (!entry || !entry->vdecl) {
        fprintf(stderr, "in ref, %s not found or not a variable\n", ident);
        ++typecheck->errors;
        return &typecheck->error_type;
      }

      struct ast_ty resolved = resolve_type(typecheck, expr_ty);
      free_ty(&ast->ty, 0);
      ast->ty = ptr_type(resolved);
      return &ast->ty;
    } break;

    case AST_EXPR_TYPE_LOAD: {
      struct ast_expr *expr = ast->load.expr;

      struct ast_ty *expr_ty = typecheck_expr(typecheck, expr);
      if (!expr_ty) {
        return NULL;
      }

      if (expr_ty->ty != AST_TYPE_POINTER) {
        fprintf(stderr, "load expression must resolve to a pointer\n");
        ++typecheck->errors;
        return &typecheck->error_type;
      }

      ast->ty = resolve_type(typecheck, ptr_pointee_type(expr_ty));
      return &ast->ty;
    } break;

    case AST_EXPR_TYPE_UNARY: {
      struct ast_ty *expr_ty = typecheck_expr(typecheck, ast->unary.expr);
      if (!expr_ty) {
        return NULL;
      }

      switch (ast->unary.op) {
        case AST_UNARY_OP_NEG:
          if (expr_ty->ty != AST_TYPE_INTEGER && expr_ty->ty != AST_TYPE_FLOAT) {
            fprintf(stderr, "negation expression must resolve to an integer or float\n");
            ++typecheck->errors;
            return &typecheck->error_type;
            ;
          }

          ast->ty = resolve_type(typecheck, expr_ty);
          return &ast->ty;

        case AST_UNARY_OP_NOT:
          if (expr_ty->ty != AST_TYPE_INTEGER) {
            fprintf(stderr, "not expression must resolve to an integer\n");
            ++typecheck->errors;
            return &typecheck->error_type;
            ;
          }

          ast->ty = resolve_type(typecheck, expr_ty);
          return &ast->ty;

        case AST_UNARY_OP_COMP:
          if (expr_ty->ty != AST_TYPE_INTEGER) {
            fprintf(stderr, "complement expression must resolve to an integer\n");
            ++typecheck->errors;
            return &typecheck->error_type;
            ;
          }

          ast->ty = resolve_type(typecheck, expr_ty);
          return &ast->ty;

        default:
          fprintf(stderr, "unhandled unary op %d\n", ast->unary.op);
          ++typecheck->errors;
          return &typecheck->error_type;
          ;
      }
    } break;

    case AST_EXPR_TYPE_MATCH: {
      struct ast_ty *expr_ty = typecheck_expr(typecheck, ast->match.expr);
      if (!expr_ty) {
        return NULL;
      }

      // first pass: check that all arms have the same pattern type, and check their expressions
      struct ast_expr_match_arm *arm = ast->match.arms;
      while (arm) {
        struct ast_ty *pattern_ty =
            typecheck_pattern_match(typecheck, arm->pattern, &arm->pattern->pattern_match, expr_ty);
        if (!pattern_ty) {
          return NULL;
        }

        maybe_implicitly_convert(pattern_ty, expr_ty);

        if (!same_type(pattern_ty, expr_ty)) {
          char wantstr[256], gotstr[256];
          type_name_into(pattern_ty, wantstr, 256);
          type_name_into(expr_ty, gotstr, 256);

          typecheck_diag_expr(typecheck, ast,
                              "match pattern has incorrect type, wanted %s but got %s\n", wantstr,
                              gotstr);
          ++typecheck->errors;
          return &typecheck->error_type;
          ;
        }

        struct scope_entry *inner_var = NULL;

        if (arm->pattern->type == AST_EXPR_TYPE_PATTERN_MATCH &&
            arm->pattern->pattern_match.inner_vdecl) {
          typecheck->scope = enter_scope(typecheck->scope);

          struct scope_entry *entry = calloc(1, sizeof(struct scope_entry));
          entry->vdecl = arm->pattern->pattern_match.inner_vdecl;
          scope_insert(typecheck->scope,
                       arm->pattern->pattern_match.inner_vdecl->ident.value.identv.ident, entry);

          inner_var = entry;
        }

        struct ast_ty *arm_ty = typecheck_expr(typecheck, arm->expr);

        if (inner_var) {
          typecheck->scope = exit_scope(typecheck->scope);
        }

        if (!arm_ty) {
          return NULL;
        }

        arm = arm->next;
      }

      if (!ast->match.otherwise) {
        typecheck_diag_expr(typecheck, ast, "match expression has no otherwise arm\n");
        ++typecheck->errors;
        return &typecheck->error_type;
      }

      struct ast_ty *otherwise_ty = typecheck_expr(typecheck, ast->match.otherwise->expr);
      if (!otherwise_ty) {
        return NULL;
      }

      struct ast_ty *largest_ty = otherwise_ty;

      // second pass: check that all arms have the same type
      arm = ast->match.arms;
      while (arm) {
        struct ast_expr_match_arm *next = arm->next ? arm->next : ast->match.otherwise;
        if (next) {
          if (!maybe_implicitly_convert(&arm->expr->ty, &next->expr->ty)) {
            maybe_implicitly_convert(&next->expr->ty, &arm->expr->ty);
          }

          if (!same_type(&arm->expr->ty, &next->expr->ty)) {
            char armstr[256], nextstr[256];
            type_name_into(&arm->expr->ty, armstr, 256);
            type_name_into(&next->expr->ty, nextstr, 256);

            typecheck_diag_expr(typecheck, ast,
                                "match arm has type %s, next arm has mismatched type %s\n", armstr,
                                nextstr);
            ++typecheck->errors;
            return &typecheck->error_type;
            ;
          }

          if (wider_type(&arm->expr->ty, largest_ty)) {
            largest_ty = &arm->expr->ty;
          }
        }

        arm = arm->next;
      }

      struct ast_ty resolved = resolve_type(typecheck, largest_ty);
      free_ty(&ast->ty, 0);
      ast->ty = resolved;
      return &ast->ty;
    } break;

    case AST_EXPR_TYPE_NIL:
      ast->ty.ty = AST_TYPE_NIL;
      return &ast->ty;

    case AST_EXPR_TYPE_ENUM_INIT: {
      struct alias_entry *entry =
          kv_lookup(typecheck->aliases, ast->enum_init.enum_ty_name.value.identv.ident);
      if (!entry) {
        typecheck_diag_expr(typecheck, ast, "enum type %s not found\n",
                            ast->enum_init.enum_ty_name.value.identv.ident);
        return &typecheck->error_type;
      }

      if (entry->ty.ty != AST_TYPE_ENUM) {
        typecheck_diag_expr(typecheck, ast, "type %s is not an enum\n",
                            ast->enum_init.enum_ty_name.value.identv.ident);
        return &typecheck->error_type;
      }

      struct ast_enum_field *field = entry->ty.enumty.fields;
      while (field) {
        if (!strcmp(field->name, ast->enum_init.enum_val_name.value.identv.ident)) {
          break;
        }
        field = field->next;
      }

      if (!field) {
        typecheck_diag_expr(typecheck, ast, "enum field %s not found in enum %s\n",
                            ast->enum_init.enum_val_name.value.identv.ident,
                            ast->enum_init.enum_ty_name.value.identv.ident);
        return &typecheck->error_type;
      }

      if (ast->enum_init.inner) {
        if (!field->has_inner) {
          typecheck_diag_expr(typecheck, ast, "enum field %s does not have an inner\n",
                              field->name);
          return &typecheck->error_type;
        }

        // includes an inner, ensure it matches the enum field's type
        struct ast_ty *inner_ty = typecheck_expr(typecheck, ast->enum_init.inner);
        if (!inner_ty) {
          return NULL;
        }

        maybe_implicitly_convert(inner_ty, &field->inner);

        struct ast_ty resolved = resolve_type(typecheck, &field->inner);
        free_ty(&field->inner, 0);
        field->inner = resolved;

        if (field->inner.ty != AST_TYPE_CUSTOM) {
          // can't check types if the inner is as yet unresolved
          if (!same_type(inner_ty, &field->inner)) {
            char innerstr[256], fieldstr[256];
            type_name_into(inner_ty, innerstr, 256);
            type_name_into(&field->inner, fieldstr, 256);

            typecheck_diag_expr(typecheck, ast,
                                "enum field %s wraps type %s, but was given type %s instead\n",
                                field->name, fieldstr, innerstr);
            return &typecheck->error_type;
          }
        }
      }

      ast->ty = resolve_type(typecheck, &entry->ty);
      return &ast->ty;
    } break;

    case AST_EXPR_TYPE_PATTERN_MATCH: {
      typecheck_diag_expr(typecheck, ast, "typecheck: pattern match without a match expression\n");
    } break;

    case AST_EXPR_TYPE_SIZEOF: {
      int swapped_type = 0;
      if (ast->sizeof_expr.expr) {
        if (ast->sizeof_expr.expr->type == AST_EXPR_TYPE_VARIABLE) {
          // is it actually a type?
          struct alias_entry *entry = kv_lookup(
              typecheck->aliases, ast->sizeof_expr.expr->variable.ident.value.identv.ident);
          if (entry) {
            ast->sizeof_expr.ty = entry->ty;
            free_expr(ast->sizeof_expr.expr);
            ast->sizeof_expr.expr = NULL;
            swapped_type = 1;
          }
        }

        if (!swapped_type) {
          struct ast_ty *expr_ty = typecheck_expr(typecheck, ast->sizeof_expr.expr);
          if (!expr_ty) {
            return NULL;
          }
        }
      }

      if (!ast->sizeof_expr.expr) {
        ast->sizeof_expr.ty = resolve_type(typecheck, &ast->sizeof_expr.ty);
      }

      ast->ty.ty = AST_TYPE_INTEGER;
      ast->ty.integer.is_signed = 1;
      ast->ty.integer.width = 32;
      ast->ty = resolve_type(typecheck, &ast->ty);
      return &ast->ty;
    } break;

    case AST_EXPR_TYPE_BOX: {
      struct ast_ty *inner_ty = NULL;

      int swapped_type = 0;
      if (ast->box_expr.expr) {
        if (ast->box_expr.expr->type == AST_EXPR_TYPE_VARIABLE) {
          // is it actually a type?
          struct alias_entry *entry =
              kv_lookup(typecheck->aliases, ast->box_expr.expr->variable.ident.value.identv.ident);
          if (entry) {
            inner_ty = &entry->ty;
            free_expr(ast->box_expr.expr);
            ast->box_expr.expr = NULL;
            swapped_type = 1;

            ast->box_expr.ty = calloc(1, sizeof(struct ast_ty));
            *ast->box_expr.ty = copy_type(inner_ty);
          }
        }

        if (!swapped_type) {
          inner_ty = typecheck_expr(typecheck, ast->box_expr.expr);
          if (!inner_ty) {
            return NULL;
          }
        }
      }

      ast->ty.ty = AST_TYPE_BOX;
      ast->ty.pointer.pointee = calloc(1, sizeof(struct ast_ty));
      *ast->ty.pointer.pointee = copy_type(inner_ty);
      return &ast->ty;
    } break;

    case AST_EXPR_TYPE_UNBOX: {
      struct ast_ty *inner_ty = typecheck_expr(typecheck, ast->box_expr.expr);
      if (!inner_ty) {
        return NULL;
      }

      if (inner_ty->ty != AST_TYPE_BOX) {
        typecheck_diag_expr(typecheck, ast, "unbox can only be used with boxed types\n");
        return &typecheck->error_type;
      }

      // returns the underlying type of the box
      ast->ty = resolve_type(typecheck, inner_ty->pointer.pointee);
      return &ast->ty;
    } break;

    default:
      typecheck_diag_expr(typecheck, ast, "typecheck: unhandled expression type %d\n", ast->type);
  }

  // all expressions must resolve to a type
  return &typecheck->void_type;
}
