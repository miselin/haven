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
    compiler_log(
        typecheck->compiler, LogLevelDebug, "typecheck",
        "TBD type from expr type %d at %s:%zd, not allowed here (not typecheck_expr_with_tbds)",
        ast->type, ast->loc.file, ast->loc.line);
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
  compiler_log(typecheck->compiler, LogLevelDebug, "typecheck", "typecheck expr %d @ %s:%zd:%zd",
               ast->type, ast->loc.file, ast->loc.line, ast->loc.column);
  switch (ast->type) {
    case AST_EXPR_TYPE_CONSTANT: {
      ast->ty = resolve_parsed_type(typecheck, &ast->parsed_ty);

      switch (ast->ty->ty) {
        case AST_TYPE_FVEC:
        case AST_TYPE_ARRAY:
        case AST_TYPE_MATRIX: {
          struct ast_expr_list *node = ast->list;
          while (node) {
            struct ast_ty *ty = typecheck_expr(typecheck, node->expr);
            if (!ty) {
              return NULL;
            }

            if (ast->ty->ty == AST_TYPE_ARRAY) {
              if (ast->ty->array.element_ty->ty == AST_TYPE_MATRIX) {
                // swap to matrix type after checking the inners are actually fvecs
                if (ty->ty != AST_TYPE_FVEC) {
                  typecheck_diag_expr(typecheck, node->expr,
                                      "matrix initializer has non-fvec type\n");
                  return NULL;
                }
              }

              maybe_implicitly_convert(&node->expr->ty, &ast->ty->array.element_ty);
            }

            node = node->next;
          }

          // convert from array to full matrix type
          if (ast->ty->ty == AST_TYPE_ARRAY) {
            if (ast->ty->array.element_ty->ty == AST_TYPE_MATRIX) {
              size_t correct_cols = ast->ty->array.element_ty->matrix.cols;
              size_t correct_rows = ast->ty->array.element_ty->matrix.rows;

              size_t rows = ast->list->num_elements;
              size_t cols = ast->list->expr->list->num_elements;

              struct ast_ty new_ty;
              memset(&new_ty, 0, sizeof(new_ty));
              new_ty.ty = AST_TYPE_MATRIX;
              new_ty.matrix.cols = cols;
              new_ty.matrix.rows = rows;

              ast->ty = type_repository_lookup_ty(typecheck->type_repo, &new_ty);
              if (!ast->ty) {
                ast->ty = type_repository_register(typecheck->type_repo, &new_ty);
              }

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
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_STRUCT_INIT: {
      ast->ty = resolve_parsed_type(typecheck, ast->parsed_ty.array.element_ty);

      // the element_ty was just a carrier for the struct type, we can free it now
      free(ast->parsed_ty.array.element_ty);
      ast->parsed_ty.array.element_ty = NULL;

      struct ast_struct_field *field = ast->ty->structty.fields;
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

        {
          struct ast_ty *expr_ty = typecheck_expr(typecheck, node->expr);
          if (!expr_ty) {
            return NULL;
          }
        }

        maybe_implicitly_convert(&node->expr->ty, &field->ty);

        if (!same_type(node->expr->ty, field->ty)) {
          char exprty[256];
          char fieldty[256];
          type_name_into(node->expr->ty, exprty, 256);
          type_name_into(field->ty, fieldty, 256);

          fprintf(stderr, "struct initializer field %s has type %s, expected %s\n", field->name,
                  exprty, fieldty);
          ++typecheck->errors;
        }

        node = node->next;
        field = field->next;
      }

      return ast->ty;
    } break;

    case AST_EXPR_TYPE_UNION_INIT: {
      // unions are a simplified variant of structs - they initialize one field, and that's it
      struct ast_ty *union_ty = resolve_type(typecheck, &ast->union_init.parsed_ty);

      if (type_is_error(union_ty)) {
        typecheck_diag_expr(typecheck, ast, "union type could not be resolved\n");
        return &typecheck->error_type;
      }

      // find the field
      struct ast_struct_field *field = union_ty->structty.fields;
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

      maybe_implicitly_convert(&ast->union_init.inner->ty, &field->ty);

      if (!same_type(ast->union_init.inner->ty, field->ty)) {
        char exprty[256];
        char fieldty[256];
        type_name_into(ast->union_init.inner->ty, exprty, 256);
        type_name_into(field->ty, fieldty, 256);

        fprintf(stderr, "union initializer field %s has type %s, expected %s\n",
                ast->union_init.field.value.identv.ident, exprty, fieldty);
        ++typecheck->errors;
      }

      ast->ty = resolve_type(typecheck, union_ty);
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_VARIABLE: {
      struct scope_entry *entry =
          scope_lookup(typecheck->scope, ast->variable.ident.value.identv.ident, 1);
      if (!entry) {
        fprintf(stderr, "%s not found\n", ast->variable.ident.value.identv.ident);
        ++typecheck->errors;
        return &typecheck->error_type;
      }

      struct ast_ty *ty = entry->ty;

      compiler_log(typecheck->compiler, LogLevelDebug, "typecheck", "%s has type %p",
                   ast->variable.ident.value.identv.ident, (void *)ty);

      ast->ty = ty;
      return ast->ty;
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
      memset(&int_ty, 0, sizeof(int_ty));
      int_ty.ty = AST_TYPE_INTEGER;
      int_ty.integer.is_signed = 1;
      int_ty.integer.width = 32;
      struct ast_ty *i32 = type_repository_lookup_ty(typecheck->type_repo, &int_ty);

      maybe_implicitly_convert(&ast->array_index.index->ty, &i32);

      struct ast_expr *target_expr = ast->array_index.target;

      if (target_expr->ty->ty == AST_TYPE_ARRAY) {
        ast->ty = resolve_type(typecheck, target_expr->ty->array.element_ty);
      } else if (target_expr->ty->ty == AST_TYPE_POINTER) {
        // type of expression is the type pointed to by the pointer
        ast->ty = resolve_type(typecheck, ptr_pointee_type(target_expr->ty));
      } else if (target_expr->ty->ty == AST_TYPE_BOX) {
        // type of expression is the type pointed to by the box
        ast->ty = resolve_type(typecheck, box_pointee_type(target_expr->ty));
      } else {
        typecheck_diag_expr(typecheck, ast->array_index.target,
                            "indexable type has an unimplemented type resolve\n");
        ++typecheck->errors;
        return &typecheck->error_type;
      }
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_BINARY: {
      {
        struct ast_ty *lhs = typecheck_expr(typecheck, ast->binary.lhs);
        struct ast_ty *rhs = typecheck_expr(typecheck, ast->binary.rhs);

        if (!lhs || !rhs) {
          return NULL;
        }
      }

      maybe_implicitly_convert(&ast->binary.lhs->ty, &ast->binary.rhs->ty);
      maybe_implicitly_convert(&ast->binary.rhs->ty, &ast->binary.lhs->ty);

      if (!same_type(ast->binary.lhs->ty, ast->binary.rhs->ty) &&
          !binary_mismatch_ok(ast->binary.op, ast->binary.lhs->ty, ast->binary.rhs->ty)) {
        char lhsstr[256], rhsstr[256];
        type_name_into(ast->binary.lhs->ty, lhsstr, 256);
        type_name_into(ast->binary.rhs->ty, rhsstr, 256);

        typecheck_diag_expr(typecheck, ast,
                            "binary op %s has mismatching lhs type %s, rhs type %s\n",
                            ast_binary_op_to_str(ast->binary.op), lhsstr, rhsstr);
        ++typecheck->errors;
        return &typecheck->error_type;
      }

      if (ast_binary_op_conditional(ast->binary.op) || ast_binary_op_logical(ast->binary.op)) {
        // conditionals & logicals both emit 1-bit booleans
        // don't set signed flag - booleans need to zero-extend in conversions, not sign-extend
        struct ast_ty i1;
        memset(&i1, 0, sizeof(i1));
        i1.ty = AST_TYPE_INTEGER;
        i1.integer.is_signed = 0;
        i1.integer.width = 1;
        ast->ty = type_repository_lookup_ty(typecheck->type_repo, &i1);
        return ast->ty;
      }

      ast->ty = ast->binary.lhs->ty;
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_BLOCK: {
      struct ast_ty *ty = typecheck_block(typecheck, &ast->block);
      if (!ty) {
        return NULL;
      }
      ast->ty = resolve_type(typecheck, ast->block.ty);
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_CALL: {
      struct ast_ty *function_ty = NULL;

      {
        struct scope_entry *entry =
            scope_lookup(typecheck->scope, ast->variable.ident.value.identv.ident, 1);
        if (!entry) {
          fprintf(stderr, "%s not found\n", ast->variable.ident.value.identv.ident);
          ++typecheck->errors;
          return &typecheck->error_type;
        }

        if (entry->fdecl) {
          ast->call.fdecl = entry->fdecl;

          function_ty = entry->ty;
        } else {
          if (entry->ty->ty != AST_TYPE_FUNCTION) {
            fprintf(stderr, "%s is not a function\n", ast->variable.ident.value.identv.ident);
            ++typecheck->errors;
            return &typecheck->error_type;
          }

          function_ty = entry->ty;
        }
      }

      if (!ast->call.args) {
        // no arguments passed
        if (function_ty->function.num_params > 0) {
          fprintf(stderr, "function %s called with no arguments, expected %zu\n",
                  ast->variable.ident.value.identv.ident, function_ty->function.num_params);
          ++typecheck->errors;
          return &typecheck->error_type;
          ;
        }
      } else if (function_ty->function.num_params != ast->call.args->num_elements) {
        if (ast->call.args->num_elements < function_ty->function.num_params &&
            !function_ty->function.vararg) {
          fprintf(stderr, "function %s called with %zu arguments, expected %zu\n",
                  ast->variable.ident.value.identv.ident, ast->call.args->num_elements,
                  function_ty->function.num_params);
          ++typecheck->errors;
          return &typecheck->error_type;
          ;
        }
      }

      struct ast_expr_list *args = ast->call.args;
      size_t i = 0;
      while (args) {
        {
          struct ast_ty *arg_ty = typecheck_expr(typecheck, args->expr);
          if (!arg_ty) {
            return NULL;
          }
        }

        // check named parameters, don't check varargs (no types to check)
        if (i < function_ty->function.num_params) {
          maybe_implicitly_convert(&args->expr->ty, &function_ty->function.param_types[i]);
          if (!same_type(args->expr->ty, function_ty->function.param_types[i])) {
            char tystr[256], expectedstr[256];
            type_name_into(args->expr->ty, tystr, 256);
            type_name_into(function_ty->function.param_types[i], expectedstr, 256);

            fprintf(stderr, "function %s argument %zu has type %s, expected %s\n",
                    ast->variable.ident.value.identv.ident, i + 1, tystr, expectedstr);
            ++typecheck->errors;
          }
        }

        args = args->next;
        ++i;
      }

      ast->call.function_ty = function_ty;
      ast->ty = resolve_type(typecheck, function_ty->function.retty);
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_DEREF: {
      {
        struct ast_ty *target_ty = typecheck_expr(typecheck, ast->deref.target);
        if (!target_ty) {
          return NULL;
        }
      }

      const char *ident = ast_expr_ident(ast);

      struct ast_ty *ty = ast->deref.target->ty;
      if (ast->deref.is_ptr) {
        if (ty->ty == AST_TYPE_POINTER) {
          ty = ptr_pointee_type(ty);
        } else if (ty->ty == AST_TYPE_BOX) {
          ty = box_pointee_type(ty);
        } else {
          fprintf(stderr, "deref of %s has non-pointer type %d\n", ident, ty->ty);
          ++typecheck->errors;
          return &typecheck->error_type;
        }

        ty = type_repository_lookup_ty(typecheck->type_repo, ty);
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

        struct ast_ty lookup_ty;
        memset(&lookup_ty, 0, sizeof(lookup_ty));
        lookup_ty.ty = AST_TYPE_FLOAT;
        ast->ty = type_repository_lookup_ty(typecheck->type_repo, &lookup_ty);
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

        ast->ty = field->ty;
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

        struct ast_ty vec_ty;
        memset(&vec_ty, 0, sizeof(vec_ty));
        vec_ty.ty = AST_TYPE_FVEC;
        vec_ty.fvec.width = ty->matrix.cols;
        ast->ty = type_repository_lookup_ty(typecheck->type_repo, &vec_ty);
        if (!ast->ty) {
          ast->ty = type_repository_register(typecheck->type_repo, &vec_ty);
        }
      }

      // can't deref past the width of the vector
      if (ast->deref.field_idx >= max_field) {
        fprintf(stderr, "deref %s has field #%zd, exceeding field count of %zd\n", ident,
                ast->deref.field_idx, max_field);
        ++typecheck->errors;
        return &typecheck->error_type;
      }

      return ast->ty;
    }; break;

    case AST_EXPR_TYPE_VOID:
      ast->ty = type_repository_void(typecheck->type_repo);
      return ast->ty;

    case AST_EXPR_TYPE_CAST: {
      struct ast_ty *resolved = resolve_parsed_type(typecheck, &ast->cast.parsed_ty);
      if (!resolved) {
        return NULL;
      }

      {
        struct ast_ty *expr_ty = typecheck_expr(typecheck, ast->cast.expr);
        if (!expr_ty) {
          return NULL;
        }
      }

      if (!can_cast(resolved, ast->cast.expr->ty)) {
        char tystr[256], exprstr[256];
        type_name_into(resolved, tystr, 256);
        type_name_into(ast->cast.expr->ty, exprstr, 256);

        fprintf(stderr, "incompatible cast from %s to %s\n", exprstr, tystr);
        ++typecheck->errors;
        return &typecheck->error_type;
      }

      ast->ty = resolved;
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_IF: {
      {
        struct ast_ty *cond = typecheck_expr(typecheck, ast->if_expr.cond);
        if (!cond) {
          return NULL;
        }

        struct ast_ty *then_ty = typecheck_block(typecheck, &ast->if_expr.then_block);
        if (!then_ty) {
          return NULL;
        }
      }

      if (ast->if_expr.elseifs) {
        struct ast_expr_elseif *elseif = ast->if_expr.elseifs;
        while (elseif) {
          {
            struct ast_ty *cond_ty = typecheck_expr(typecheck, elseif->cond);
            if (!cond_ty) {
              return NULL;
            }

            struct ast_ty *block_ty = typecheck_block(typecheck, &elseif->block);
            if (!block_ty) {
              return NULL;
            }
          }

          maybe_implicitly_convert(&elseif->block.ty, &ast->if_expr.then_block.ty);

          if (!same_type(ast->if_expr.then_block.ty, elseif->block.ty)) {
            char thenstr[256], blockstr[256];
            type_name_into(ast->if_expr.then_block.ty, thenstr, 256);
            type_name_into(elseif->block.ty, blockstr, 256);

            fprintf(stderr, "elseif block has type %s, expected %s\n", blockstr, thenstr);
            ++typecheck->errors;
            return &typecheck->error_type;
          }

          elseif = elseif->next;
        }
      }

      if (ast->if_expr.has_else) {
        {
          struct ast_ty *else_ty = typecheck_block(typecheck, &ast->if_expr.else_block);
          if (!else_ty) {
            return NULL;
          }
        }

        maybe_implicitly_convert(&ast->if_expr.else_block.ty, &ast->if_expr.then_block.ty);

        if (!same_type(ast->if_expr.then_block.ty, ast->if_expr.else_block.ty)) {
          char thenstr[256], elsestr[256];
          type_name_into(ast->if_expr.then_block.ty, thenstr, 256);
          type_name_into(ast->if_expr.else_block.ty, elsestr, 256);

          fprintf(stderr, "if then block has type %s, else block has type %s\n", thenstr, elsestr);
          ++typecheck->errors;
          return &typecheck->error_type;
        }
      }

      if (ast->if_expr.then_block.ty->ty != AST_TYPE_VOID && !ast->if_expr.has_else) {
        fprintf(stderr, "an else block is required when if is used as an expression\n");
        ++typecheck->errors;
        return &typecheck->error_type;
      }

      ast->ty = ast->if_expr.then_block.ty;
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_ASSIGN: {
      {
        struct ast_ty *lhs_ty = typecheck_expr(typecheck, ast->assign.lhs);
        if (!lhs_ty) {
          return NULL;
        }

        struct ast_ty *expr_ty = typecheck_expr_with_tbds(typecheck, ast->assign.expr);
        if (!expr_ty) {
          return NULL;
        }
      }

      const char *ident = ast_expr_ident(ast->assign.lhs);

      {
        if (!ident) {
          compiler_log(typecheck->compiler, LogLevelDebug, "typecheck", "assign lhs not an ident");
          return NULL;  // TODO: this should happen in semantic pass not here
        }

        struct scope_entry *entry = scope_lookup(typecheck->scope, ident, 1);
        if (!entry || entry->fdecl) {
          fprintf(stderr, "in assignment, %s not found or not a variable\n", ident);
          ++typecheck->errors;
          return &typecheck->error_type;
        }

        if (!(entry->decl_flags & DECL_FLAG_MUT)) {
          fprintf(stderr, "%s is not mutable\n", ident);
          ++typecheck->errors;
          return &typecheck->error_type;
        }
      }

      struct ast_ty **desired_ty = &ast->assign.lhs->ty;

      const char *field_name = NULL;

      if (ast->assign.lhs->type == AST_EXPR_TYPE_DEREF) {
        // all the checks in this block are probably already handled fine by DEREF, but let's make
        // sure

        struct ast_ty *deref_ty = ast->assign.lhs->deref.target->ty;
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

          deref_ty = type_repository_lookup_ty(typecheck->type_repo, deref_ty);
        }

        if (deref_ty->ty != AST_TYPE_STRUCT) {
          char tyname[256];
          type_name_into(ast->assign.lhs->ty, tyname, 256);
          fprintf(stderr, "in field assignment, lhs %s has non-struct type %s\n", ident, tyname);
          ++typecheck->errors;
          return &typecheck->error_type;
        }

        struct ast_struct_field *field = deref_ty->structty.fields;
        while (field) {
          if (strcmp(field->name, ast->assign.lhs->deref.field.value.identv.ident) == 0) {
            field_name = field->name;
            desired_ty = &field->ty;
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

      maybe_implicitly_convert(&ast->assign.expr->ty, desired_ty);

      if (!same_type(*desired_ty, ast->assign.expr->ty)) {
        char tystr[256], exprstr[256];
        type_name_into(*desired_ty, tystr, 256);
        type_name_into(ast->assign.expr->ty, exprstr, 256);

        if (field_name) {
          fprintf(stderr, "field assignment to %s.%s has type %s, expected %s\n", ident, field_name,
                  exprstr, tystr);
        } else {
          fprintf(stderr, "assignment to %s has type %s, expected %s\n", ident, exprstr, tystr);
        }
        ++typecheck->errors;
        return &typecheck->error_type;
      }

      ast->ty = ast->assign.expr->ty;
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_REF: {
      compiler_log(typecheck->compiler, LogLevelDebug, "typecheck", "typechecking ref");

      struct ast_expr *expr = ast->ref.expr;

      {
        struct ast_ty *expr_ty = typecheck_expr(typecheck, expr);
        if (!expr_ty) {
          return NULL;
        }
      }

      if (expr->type != AST_EXPR_TYPE_VARIABLE) {
        fprintf(stderr, "ref expression must resolve to an identifier\n");
        ++typecheck->errors;
        return &typecheck->error_type;
      }

      const char *ident = expr->variable.ident.value.identv.ident;

      struct scope_entry *entry = scope_lookup(typecheck->scope, ident, 1);
      if (!entry || entry->fdecl) {
        fprintf(stderr, "in ref, %s not found or not a variable\n", ident);
        ++typecheck->errors;
        return &typecheck->error_type;
      }

      struct ast_ty wrapped;
      memset(&wrapped, 0, sizeof(wrapped));
      wrapped.ty = AST_TYPE_POINTER;
      wrapped.pointer.pointee = expr->ty;
      ast->ty = type_repository_lookup_ty(typecheck->type_repo, &wrapped);
      if (!ast->ty) {
        ast->ty = type_repository_register(typecheck->type_repo, &wrapped);
      }
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_LOAD: {
      struct ast_expr *expr = ast->load.expr;

      {
        struct ast_ty *expr_ty = typecheck_expr(typecheck, expr);
        if (!expr_ty) {
          return NULL;
        }
      }

      if (expr->ty->ty != AST_TYPE_POINTER) {
        fprintf(stderr, "load expression must resolve to a pointer\n");
        ++typecheck->errors;
        return &typecheck->error_type;
      }

      ast->ty = type_repository_lookup_ty(typecheck->type_repo, ptr_pointee_type(expr->ty));
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_UNARY: {
      {
        struct ast_ty *expr_ty = typecheck_expr(typecheck, ast->unary.expr);
        if (!expr_ty) {
          return NULL;
        }
      }

      switch (ast->unary.op) {
        case AST_UNARY_OP_NEG:
          if (ast->unary.expr->ty->ty != AST_TYPE_INTEGER &&
              ast->unary.expr->ty->ty != AST_TYPE_FLOAT) {
            fprintf(stderr, "negation expression must resolve to an integer or float\n");
            ++typecheck->errors;
            return &typecheck->error_type;
            ;
          }

          ast->ty = ast->unary.expr->ty;
          return ast->ty;

        case AST_UNARY_OP_NOT:
          if (ast->unary.expr->ty->ty != AST_TYPE_INTEGER) {
            fprintf(stderr, "not expression must resolve to an integer\n");
            ++typecheck->errors;
            return &typecheck->error_type;
            ;
          }

          ast->ty = ast->unary.expr->ty;
          return ast->ty;

        case AST_UNARY_OP_COMP:
          if (ast->unary.expr->ty->ty != AST_TYPE_INTEGER) {
            fprintf(stderr, "complement expression must resolve to an integer\n");
            ++typecheck->errors;
            return &typecheck->error_type;
            ;
          }

          ast->ty = ast->unary.expr->ty;
          return ast->ty;

        default:
          fprintf(stderr, "unhandled unary op %d\n", ast->unary.op);
          ++typecheck->errors;
          return &typecheck->error_type;
          ;
      }
    } break;

    case AST_EXPR_TYPE_MATCH: {
      {
        struct ast_ty *expr_ty = typecheck_expr(typecheck, ast->match.expr);
        if (!expr_ty) {
          return NULL;
        }
      }

      // first pass: check that all arms have the same pattern type, and check their expressions
      struct ast_expr_match_arm *arm = ast->match.arms;
      while (arm) {
        {
          struct ast_ty *pattern_ty = typecheck_pattern_match(
              typecheck, arm->pattern, &arm->pattern->pattern_match, ast->match.expr->ty);
          if (!pattern_ty) {
            return NULL;
          }
        }

        maybe_implicitly_convert(&arm->pattern->ty, &ast->match.expr->ty);

        if (!same_type(arm->pattern->ty, ast->match.expr->ty)) {
          char wantstr[256], gotstr[256];
          type_name_into(arm->pattern->ty, wantstr, 256);
          type_name_into(ast->match.expr->ty, gotstr, 256);

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
          entry->ty = arm->pattern->pattern_match.inner_vdecl->ty;
          entry->decl_flags = arm->pattern->pattern_match.inner_vdecl->flags;
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

          if (!same_type(arm->expr->ty, next->expr->ty)) {
            char armstr[256], nextstr[256];
            type_name_into(arm->expr->ty, armstr, 256);
            type_name_into(next->expr->ty, nextstr, 256);

            typecheck_diag_expr(typecheck, ast,
                                "match arm has type %s, next arm has mismatched type %s\n", armstr,
                                nextstr);
            ++typecheck->errors;
            return &typecheck->error_type;
            ;
          }

          if (wider_type(arm->expr->ty, largest_ty)) {
            largest_ty = arm->expr->ty;
          }
        }

        arm = arm->next;
      }

      ast->ty = largest_ty;
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_NIL: {
      struct ast_ty lookup;
      memset(&lookup, 0, sizeof(lookup));
      lookup.ty = AST_TYPE_NIL;
      ast->ty = type_repository_lookup_ty(typecheck->type_repo, &lookup);
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_ENUM_INIT: {
      const char *ident = ast->enum_init.enum_ty_name.value.identv.ident;
      struct ast_ty *enumty = type_repository_lookup(typecheck->type_repo, ident);
      if (!enumty) {
        typecheck_diag_expr(typecheck, ast, "enum type %s not found\n", ident);
        return &typecheck->error_type;
      }

      if (enumty->ty != AST_TYPE_ENUM) {
        typecheck_diag_expr(typecheck, ast, "type %s is not an enum\n",
                            ast->enum_init.enum_ty_name.value.identv.ident);
        return &typecheck->error_type;
      }

      struct ast_enum_field *field = enumty->enumty.fields;
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
        {
          struct ast_ty *inner_ty = typecheck_expr(typecheck, ast->enum_init.inner);
          if (!inner_ty) {
            return NULL;
          }
        }

        maybe_implicitly_convert(&ast->enum_init.inner->ty, &field->inner);

        if (field->inner->ty != AST_TYPE_CUSTOM) {
          // can't check types if the inner is as yet unresolved
          if (!same_type(ast->enum_init.inner->ty, field->inner)) {
            char innerstr[256], fieldstr[256];
            type_name_into(ast->enum_init.inner->ty, innerstr, 256);
            type_name_into(field->inner, fieldstr, 256);

            typecheck_diag_expr(typecheck, ast,
                                "enum field %s wraps type %s, but was given type %s instead\n",
                                field->name, fieldstr, innerstr);
            return &typecheck->error_type;
          }
        } else {
          compiler_log(typecheck->compiler, LogLevelDebug, "typecheck",
                       "enum field %s has unresolved inner type", field->name);
        }
      }

      ast->ty = enumty;
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_PATTERN_MATCH: {
      typecheck_diag_expr(typecheck, ast, "typecheck: pattern match without a match expression\n");
    } break;

    case AST_EXPR_TYPE_SIZEOF: {
      struct ast_ty *inner_ty = NULL;

      int swapped_type = 0;
      if (ast->sizeof_expr.expr) {
        // the parser can't distinguish between types and identifiers, so we need to do so here
        if (ast->sizeof_expr.expr->type == AST_EXPR_TYPE_VARIABLE) {
          inner_ty = type_repository_lookup(
              typecheck->type_repo, ast->sizeof_expr.expr->variable.ident.value.identv.ident);
          if (inner_ty) {
            free_expr(typecheck->compiler, ast->sizeof_expr.expr);
            ast->sizeof_expr.expr = NULL;
            swapped_type = 1;
          }
        }

        if (!swapped_type) {
          inner_ty = typecheck_expr(typecheck, ast->sizeof_expr.expr);
          if (!inner_ty) {
            return NULL;
          }

          inner_ty = ast->sizeof_expr.expr->ty;
        }
      } else {
        inner_ty = resolve_parsed_type(typecheck, &ast->sizeof_expr.parsed_ty);
      }

      ast->sizeof_expr.resolved = inner_ty;

      struct ast_ty itype;
      memset(&itype, 0, sizeof(itype));
      itype.ty = AST_TYPE_INTEGER;
      itype.integer.is_signed = 1;
      itype.integer.width = 32;
      ast->ty = resolve_type(typecheck, &itype);
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_BOX: {
      struct ast_ty *inner_ty = NULL;

      int swapped_type = 0;
      if (ast->box_expr.expr) {
        // the parser can't distinguish between types and identifiers, so we need to do so here
        if (ast->box_expr.expr->type == AST_EXPR_TYPE_VARIABLE) {
          inner_ty = type_repository_lookup(typecheck->type_repo,
                                            ast->box_expr.expr->variable.ident.value.identv.ident);
          if (inner_ty) {
            free_expr(typecheck->compiler, ast->box_expr.expr);
            ast->box_expr.expr = NULL;
            swapped_type = 1;
          }
        }

        if (!swapped_type) {
          inner_ty = typecheck_expr(typecheck, ast->box_expr.expr);
          if (!inner_ty) {
            return NULL;
          }

          inner_ty = ast->box_expr.expr->ty;
        }
      } else {
        inner_ty = resolve_parsed_type(typecheck, &ast->box_expr.parsed_ty);
      }

      struct ast_ty box_ty;
      memset(&box_ty, 0, sizeof(box_ty));
      box_ty.ty = AST_TYPE_BOX;
      box_ty.pointer.pointee = inner_ty;
      ast->ty = type_repository_lookup_ty(typecheck->type_repo, &box_ty);
      if (!ast->ty) {
        ast->ty = type_repository_register(typecheck->type_repo, &box_ty);
      }
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_UNBOX: {
      {
        struct ast_ty *inner_ty = typecheck_expr(typecheck, ast->box_expr.expr);
        if (!inner_ty) {
          return NULL;
        }
      }

      if (ast->box_expr.expr->ty->ty != AST_TYPE_BOX) {
        typecheck_diag_expr(typecheck, ast, "unbox can only be used with boxed types\n");
        return &typecheck->error_type;
      }

      // returns the underlying type of the box
      ast->ty = ast->box_expr.expr->ty->pointer.pointee;
      return ast->ty;
    } break;

    default:
      typecheck_diag_expr(typecheck, ast, "typecheck: unhandled expression type %d\n", ast->type);
  }

  // all expressions must resolve to a type
  return &typecheck->void_type;
}
