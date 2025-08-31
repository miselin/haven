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

static struct ast_ty *typecheck_initializer(struct typecheck *typecheck, struct ast_expr *ast,
                                            struct ast_ty *expected_ty);

struct ast_ty *typecheck_expr(struct typecheck *typecheck, struct ast_expr *ast,
                              struct ast_ty *expected_ty) {
  struct ast_ty *ty = typecheck_expr_with_tbds(typecheck, ast, expected_ty);
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

struct ast_ty *typecheck_expr_with_tbds(struct typecheck *typecheck, struct ast_expr *ast,
                                        struct ast_ty *expected_ty) {
  struct ast_ty *ty = typecheck_expr_inner(typecheck, ast, expected_ty);
  if (!ty) {
    return ty;
  }

  if (type_is_error(ty)) {
    typecheck_diag_expr(typecheck, ast, "type failed to resolve\n");
    return NULL;
  }

  return ty;
}

struct ast_ty *typecheck_expr_inner(struct typecheck *typecheck, struct ast_expr *ast,
                                    struct ast_ty *expected_ty) {
  compiler_log(typecheck->compiler, LogLevelDebug, "typecheck", "typecheck expr %d @ %s:%zd:%zd",
               ast->type, ast->loc.file, ast->loc.line, ast->loc.column);
  switch (ast->type) {
    case AST_EXPR_TYPE_CONSTANT: {
      ast->ty = resolve_parsed_type(typecheck, &ast->parsed_ty);
      if (!ast->ty) {
        ast->ty = expected_ty;
      }

      switch (ast->ty->ty) {
        case AST_TYPE_FVEC:
        case AST_TYPE_ARRAY:
        case AST_TYPE_MATRIX: {
          struct ast_expr_list *node = ast->expr.list;

          struct ast_ty *expected_element_ty = NULL;
          if (expected_ty) {
            // help type inference by passing down an expected type, if we know it
            if (expected_ty->ty == AST_TYPE_ARRAY) {
              expected_element_ty = expected_ty->oneof.array.element_ty;
            } else if (expected_ty->ty == AST_TYPE_FVEC) {
              expected_element_ty = type_repository_lookup(typecheck->type_repo, "float");
            }
          }

          while (node) {
            struct ast_ty *ty = typecheck_expr(typecheck, node->expr, expected_element_ty);
            if (!ty) {
              return NULL;
            }

            if (ast->ty->ty == AST_TYPE_ARRAY) {
              if (ast->ty->oneof.array.element_ty->ty == AST_TYPE_MATRIX) {
                // swap to matrix type after checking the inners are actually fvecs
                if (ty->ty != AST_TYPE_FVEC) {
                  typecheck_diag_expr(typecheck, node->expr,
                                      "matrix initializer has non-fvec type\n");
                  return NULL;
                }
              }

              maybe_implicitly_convert(&node->expr->ty, &ast->ty->oneof.array.element_ty);
            }

            node = node->next;
          }

          if (ast->ty->ty == AST_TYPE_MATRIX && ast->ty->oneof.matrix.cols == 0) {
            // now we know the type of the inner expressions
            struct ast_expr *first_expr = ast->expr.list->expr;
            if (first_expr->ty->ty != AST_TYPE_FVEC) {
              typecheck_diag_expr(typecheck, first_expr,
                                  "matrix initializer has non-fvec type for first element\n");
              return NULL;
            }

            ast->ty->oneof.matrix.cols = first_expr->ty->oneof.fvec.width;

            size_t element = 1;

            struct ast_expr_list *check_node = ast->expr.list->next;
            while (check_node) {
              maybe_implicitly_convert(&check_node->expr->ty, &first_expr->ty);

              if (check_node->expr->ty->ty != first_expr->ty->ty) {
                typecheck_diag_expr(typecheck, check_node->expr,
                                    "matrix initializer has non-fvec type for element %zu\n",
                                    element);
                return NULL;
              }

              if (check_node->expr->ty->oneof.fvec.width != first_expr->ty->oneof.fvec.width) {
                typecheck_diag_expr(typecheck, check_node->expr,
                                    "matrix initializer element %zu has type fvec%zu, "
                                    "wanted fvec%zu\n",
                                    element, check_node->expr->ty->oneof.fvec.width,
                                    first_expr->ty->oneof.fvec.width);
                return NULL;
              }

              check_node = check_node->next;
              ++element;
            }
          }
        } break;
        default:
          break;
      }
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_INITIALIZER: {
      ast->ty = typecheck_initializer(typecheck, ast, expected_ty);
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_VARIABLE: {
      struct scope_entry *entry =
          scope_lookup(typecheck->scope, ast->expr.variable.ident.value.identv.ident, 1);
      if (!entry) {
        fprintf(stderr, "%s not found\n", ast->expr.variable.ident.value.identv.ident);
        ++typecheck->errors;
        return &typecheck->error_type;
      }

      struct ast_ty *ty = entry->ty;

      compiler_log(typecheck->compiler, LogLevelDebug, "typecheck", "%s has type %p",
                   ast->expr.variable.ident.value.identv.ident, (void *)ty);

      ast->ty = ty;
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_ARRAY_INDEX: {
      struct ast_ty *target_ty =
          typecheck_expr(typecheck, ast->expr.array_index.target, expected_ty);
      if (!target_ty) {
        return NULL;
      }

      const char *ident = ast_expr_ident(ast->expr.array_index.target);

      // is the target indexable?
      if (!type_is_indexable(target_ty)) {
        typecheck_diag_expr(typecheck, ast->expr.array_index.target, "%s is not indexable\n",
                            ident);
        ++typecheck->errors;
        return &typecheck->error_type;
      }

      // try to coerce the index to a 32-bit integer
      struct ast_ty *index_ty = typecheck_expr(typecheck, ast->expr.array_index.index, NULL);
      if (!index_ty) {
        return NULL;
      }

      struct ast_ty int_ty;
      memset(&int_ty, 0, sizeof(int_ty));
      int_ty.ty = AST_TYPE_INTEGER;
      int_ty.oneof.integer.is_signed = 1;
      int_ty.oneof.integer.width = 32;
      struct ast_ty *i32 = type_repository_lookup_ty(typecheck->type_repo, &int_ty);

      maybe_implicitly_convert(&ast->expr.array_index.index->ty, &i32);

      struct ast_expr *target_expr = ast->expr.array_index.target;

      if (target_expr->ty->ty == AST_TYPE_ARRAY) {
        ast->ty = resolve_type(typecheck, target_expr->ty->oneof.array.element_ty);
      } else if (target_expr->ty->ty == AST_TYPE_POINTER) {
        // type of expression is the type pointed to by the pointer
        ast->ty = resolve_type(typecheck, ptr_pointee_type(target_expr->ty));
      } else if (target_expr->ty->ty == AST_TYPE_BOX) {
        // type of expression is the type pointed to by the box
        ast->ty = resolve_type(typecheck, box_pointee_type(target_expr->ty));
      } else {
        typecheck_diag_expr(typecheck, ast->expr.array_index.target,
                            "indexable type has an unimplemented type resolve\n");
        ++typecheck->errors;
        return &typecheck->error_type;
      }
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_BINARY: {
      {
        struct ast_ty *lhs = typecheck_expr(typecheck, ast->expr.binary.lhs, NULL);
        struct ast_ty *rhs = typecheck_expr(typecheck, ast->expr.binary.rhs, NULL);

        if (!lhs || !rhs) {
          return NULL;
        }
      }

      maybe_implicitly_convert(&ast->expr.binary.lhs->ty, &ast->expr.binary.rhs->ty);
      maybe_implicitly_convert(&ast->expr.binary.rhs->ty, &ast->expr.binary.lhs->ty);

      if (!same_type(ast->expr.binary.lhs->ty, ast->expr.binary.rhs->ty) &&
          !binary_mismatch_ok(ast->expr.binary.op, ast->expr.binary.lhs->ty,
                              ast->expr.binary.rhs->ty)) {
        char lhsstr[256], rhsstr[256];
        type_name_into(ast->expr.binary.lhs->ty, lhsstr, 256);
        type_name_into(ast->expr.binary.rhs->ty, rhsstr, 256);

        typecheck_diag_expr(typecheck, ast,
                            "binary op %s has mismatching lhs type %s, rhs type %s\n",
                            ast_binary_op_to_str(ast->expr.binary.op), lhsstr, rhsstr);
        ++typecheck->errors;
        return &typecheck->error_type;
      }

      if (ast_binary_op_conditional(ast->expr.binary.op) ||
          ast_binary_op_logical(ast->expr.binary.op)) {
        // conditionals & logicals both emit 1-bit booleans
        // don't set signed flag - booleans need to zero-extend in conversions, not sign-extend
        struct ast_ty i1;
        memset(&i1, 0, sizeof(i1));
        i1.ty = AST_TYPE_INTEGER;
        i1.oneof.integer.is_signed = 0;
        i1.oneof.integer.width = 1;
        ast->ty = type_repository_lookup_ty(typecheck->type_repo, &i1);
        return ast->ty;
      }

      ast->ty = ast->expr.binary.lhs->ty;
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_BLOCK: {
      struct ast_ty *ty = typecheck_block(typecheck, &ast->expr.block, ast->ty);
      if (!ty) {
        return NULL;
      }
      ast->ty = resolve_type(typecheck, ast->expr.block.ty);
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_CALL: {
      struct ast_ty *function_ty = NULL;

      {
        struct scope_entry *entry =
            scope_lookup(typecheck->scope, ast->expr.variable.ident.value.identv.ident, 1);
        if (!entry) {
          fprintf(stderr, "%s not found\n", ast->expr.variable.ident.value.identv.ident);
          ++typecheck->errors;
          return &typecheck->error_type;
        }

        if (entry->fdecl) {
          ast->expr.call.fdecl = entry->fdecl;

          function_ty = entry->ty;
        } else {
          if (entry->ty->ty != AST_TYPE_FUNCTION) {
            fprintf(stderr, "%s is not a function\n", ast->expr.variable.ident.value.identv.ident);
            ++typecheck->errors;
            return &typecheck->error_type;
          }

          function_ty = entry->ty;
        }
      }

      if (!ast->expr.call.args) {
        // no arguments passed
        if (function_ty->oneof.function.num_params > 0) {
          fprintf(stderr, "function %s called with no arguments, expected %zu\n",
                  ast->expr.variable.ident.value.identv.ident,
                  function_ty->oneof.function.num_params);
          ++typecheck->errors;
          return &typecheck->error_type;
          ;
        }
      } else if (function_ty->oneof.function.num_params != ast->expr.call.args->num_elements) {
        if (ast->expr.call.args->num_elements < function_ty->oneof.function.num_params &&
            !function_ty->oneof.function.vararg) {
          fprintf(stderr, "function %s called with %zu arguments, expected %zu\n",
                  ast->expr.variable.ident.value.identv.ident, ast->expr.call.args->num_elements,
                  function_ty->oneof.function.num_params);
          ++typecheck->errors;
          return &typecheck->error_type;
          ;
        }
      }

      struct ast_expr_list *args = ast->expr.call.args;
      size_t i = 0;
      while (args) {
        {
          struct ast_ty *arg_ty =
              typecheck_expr(typecheck, args->expr, function_ty->oneof.function.param_types[i]);
          if (!arg_ty) {
            return NULL;
          }
        }

        // check named parameters, don't check varargs (no types to check)
        if (i < function_ty->oneof.function.num_params) {
          maybe_implicitly_convert(&args->expr->ty, &function_ty->oneof.function.param_types[i]);
          if (!same_type(args->expr->ty, function_ty->oneof.function.param_types[i])) {
            char tystr[256], expectedstr[256];
            type_name_into(args->expr->ty, tystr, 256);
            type_name_into(function_ty->oneof.function.param_types[i], expectedstr, 256);

            fprintf(stderr, "function %s argument %zu has type %s, expected %s\n",
                    ast->expr.variable.ident.value.identv.ident, i + 1, tystr, expectedstr);
            ++typecheck->errors;
          }
        }

        args = args->next;
        ++i;
      }

      ast->expr.call.function_ty = function_ty;
      ast->ty = resolve_type(typecheck, function_ty->oneof.function.retty);
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_DEREF: {
      {
        struct ast_ty *target_ty = typecheck_expr(typecheck, ast->expr.deref.target, expected_ty);
        if (!target_ty) {
          return NULL;
        }
      }

      const char *ident = ast_expr_ident(ast);

      struct ast_ty *ty = ast->expr.deref.target->ty;
      if (ast->expr.deref.is_ptr) {
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
        int deref = deref_to_index(ast->expr.deref.field.value.identv.ident);
        if (deref < 0) {
          fprintf(stderr, "fvec deref %s has unknown field %s\n", ident,
                  ast->expr.deref.field.value.identv.ident);
          ++typecheck->errors;
          return &typecheck->error_type;
          ;
        }
        ast->expr.deref.field_idx = (size_t)deref;
        max_field = ty->oneof.fvec.width;

        struct ast_ty lookup_ty;
        memset(&lookup_ty, 0, sizeof(lookup_ty));
        lookup_ty.ty = AST_TYPE_FLOAT;
        ast->ty = type_repository_lookup_ty(typecheck->type_repo, &lookup_ty);
      } else if (ty->ty == AST_TYPE_STRUCT) {
        struct ast_struct_field *field = ty->oneof.structty.fields;
        if (!field) {
          compiler_log(typecheck->compiler, LogLevelDebug, "typecheck", "struct %s has no fields",
                       ident);
        }
        size_t i = 0;
        while (field) {
          compiler_log(typecheck->compiler, LogLevelDebug, "typecheck", "check field '%s' vs '%s'",
                       field->name, ast->expr.deref.field.value.identv.ident);
          if (strcmp(field->name, ast->expr.deref.field.value.identv.ident) == 0) {
            ast->expr.deref.field_idx = i;
            break;
          }
          field = field->next;
          ++i;
        }

        if (!field) {
          fprintf(stderr, "struct deref %s references unknown field %s\n", ident,
                  ast->expr.deref.field.value.identv.ident);
          ++typecheck->errors;
          return &typecheck->error_type;
          ;
        }

        ast->ty = field->ty;
        max_field = ty->oneof.structty.num_fields;
      } else if (ty->ty == AST_TYPE_MATRIX) {
        int deref = deref_to_index(ast->expr.deref.field.value.identv.ident);
        if (deref < 0) {
          fprintf(stderr, "matrix deref %s has unknown field %s\n", ident,
                  ast->expr.deref.field.value.identv.ident);
          ++typecheck->errors;
          return &typecheck->error_type;
          ;
        }
        ast->expr.deref.field_idx = (size_t)deref;
        max_field = ty->oneof.matrix.rows;

        struct ast_ty vec_ty;
        memset(&vec_ty, 0, sizeof(vec_ty));
        vec_ty.ty = AST_TYPE_FVEC;
        vec_ty.oneof.fvec.width = ty->oneof.matrix.cols;
        ast->ty = type_repository_lookup_ty(typecheck->type_repo, &vec_ty);
        if (!ast->ty) {
          ast->ty = type_repository_register(typecheck->type_repo, &vec_ty);
        }
      }

      // can't deref past the width of the vector
      if (ast->expr.deref.field_idx >= max_field) {
        fprintf(stderr, "deref %s has field #%zd, exceeding field count of %zd\n", ident,
                ast->expr.deref.field_idx, max_field);
        ++typecheck->errors;
        return &typecheck->error_type;
      }

      return ast->ty;
    }; break;

    case AST_EXPR_TYPE_VOID:
      ast->ty = type_repository_void(typecheck->type_repo);
      return ast->ty;

    case AST_EXPR_TYPE_CAST: {
      struct ast_ty *resolved = resolve_parsed_type(typecheck, &ast->expr.cast.parsed_ty);
      if (!resolved) {
        return NULL;
      }

      {
        struct ast_ty *expr_ty = typecheck_expr(typecheck, ast->expr.cast.expr, NULL);
        if (!expr_ty) {
          return NULL;
        }
      }

      if (!can_cast(resolved, ast->expr.cast.expr->ty)) {
        char tystr[256], exprstr[256];
        type_name_into(resolved, tystr, 256);
        type_name_into(ast->expr.cast.expr->ty, exprstr, 256);

        fprintf(stderr, "incompatible cast from %s to %s\n", exprstr, tystr);
        ++typecheck->errors;
        return &typecheck->error_type;
      }

      ast->ty = resolved;
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_IF: {
      {
        struct ast_ty *cond = typecheck_expr(typecheck, ast->expr.if_expr.cond, NULL);
        if (!cond) {
          return NULL;
        }

        struct ast_ty *then_ty = typecheck_block(typecheck, &ast->expr.if_expr.then_block, NULL);
        if (!then_ty) {
          return NULL;
        }
      }

      if (ast->expr.if_expr.elseifs) {
        struct ast_expr_elseif *elseif = ast->expr.if_expr.elseifs;
        while (elseif) {
          {
            struct ast_ty *cond_ty = typecheck_expr(typecheck, elseif->cond, NULL);
            if (!cond_ty) {
              return NULL;
            }

            struct ast_ty *block_ty = typecheck_block(typecheck, &elseif->block, NULL);
            if (!block_ty) {
              return NULL;
            }
          }

          maybe_implicitly_convert(&elseif->block.ty, &ast->expr.if_expr.then_block.ty);

          if (!same_type(ast->expr.if_expr.then_block.ty, elseif->block.ty)) {
            char thenstr[256], blockstr[256];
            type_name_into(ast->expr.if_expr.then_block.ty, thenstr, 256);
            type_name_into(elseif->block.ty, blockstr, 256);

            fprintf(stderr, "elseif block has type %s, expected %s\n", blockstr, thenstr);
            ++typecheck->errors;
            return &typecheck->error_type;
          }

          elseif = elseif->next;
        }
      }

      if (ast->expr.if_expr.has_else) {
        {
          struct ast_ty *else_ty = typecheck_block(typecheck, &ast->expr.if_expr.else_block, NULL);
          if (!else_ty) {
            return NULL;
          }
        }

        maybe_implicitly_convert(&ast->expr.if_expr.else_block.ty,
                                 &ast->expr.if_expr.then_block.ty);

        if (!same_type(ast->expr.if_expr.then_block.ty, ast->expr.if_expr.else_block.ty)) {
          char thenstr[256], elsestr[256];
          type_name_into(ast->expr.if_expr.then_block.ty, thenstr, 256);
          type_name_into(ast->expr.if_expr.else_block.ty, elsestr, 256);

          fprintf(stderr, "if then block has type %s, else block has type %s\n", thenstr, elsestr);
          ++typecheck->errors;
          return &typecheck->error_type;
        }
      }

      if (ast->expr.if_expr.then_block.ty->ty != AST_TYPE_VOID && !ast->expr.if_expr.has_else) {
        fprintf(stderr, "an else block is required when if is used as an expression\n");
        ++typecheck->errors;
        return &typecheck->error_type;
      }

      ast->ty = ast->expr.if_expr.then_block.ty;
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_ASSIGN: {
      {
        struct ast_ty *lhs_ty = typecheck_expr(typecheck, ast->expr.assign.lhs, NULL);
        if (!lhs_ty) {
          return NULL;
        }

        struct ast_ty *expr_ty = typecheck_expr_with_tbds(typecheck, ast->expr.assign.expr, lhs_ty);
        if (!expr_ty) {
          return NULL;
        }
      }

      const char *ident = ast_expr_ident(ast->expr.assign.lhs);

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

        if ((entry->decl_flags & DECL_FLAG_MUT) == 0) {
          fprintf(stderr, "%s is not mutable\n", ident);
          ++typecheck->errors;
          return &typecheck->error_type;
        }
      }

      struct ast_ty **desired_ty = &ast->expr.assign.lhs->ty;

      const char *field_name = NULL;

      if (ast->expr.assign.lhs->type == AST_EXPR_TYPE_DEREF) {
        // all the checks in this block are probably already handled fine by DEREF, but let's make
        // sure

        struct ast_ty *deref_ty = ast->expr.assign.lhs->expr.deref.target->ty;
        if (ast->expr.assign.lhs->expr.deref.is_ptr) {
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
          type_name_into(ast->expr.assign.lhs->ty, tyname, 256);
          fprintf(stderr, "in field assignment, lhs %s has non-struct type %s\n", ident, tyname);
          ++typecheck->errors;
          return &typecheck->error_type;
        }

        struct ast_struct_field *field = deref_ty->oneof.structty.fields;
        while (field) {
          if (strcmp(field->name, ast->expr.assign.lhs->expr.deref.field.value.identv.ident) == 0) {
            field_name = field->name;
            desired_ty = &field->ty;
            break;
          }
          field = field->next;
        }

        if (!field) {
          fprintf(stderr, "field %s not found in struct %s\n",
                  ast->expr.assign.lhs->expr.deref.field.value.identv.ident, ident);
          ++typecheck->errors;
          return &typecheck->error_type;
        }
      }

      maybe_implicitly_convert(&ast->expr.assign.expr->ty, desired_ty);

      if (!same_type(*desired_ty, ast->expr.assign.expr->ty)) {
        char tystr[256], exprstr[256];
        type_name_into(*desired_ty, tystr, 256);
        type_name_into(ast->expr.assign.expr->ty, exprstr, 256);

        if (field_name) {
          fprintf(stderr, "field assignment to %s.%s has type %s, expected %s\n", ident, field_name,
                  exprstr, tystr);
        } else {
          fprintf(stderr, "assignment to %s has type %s, expected %s\n", ident, exprstr, tystr);
        }
        ++typecheck->errors;
        return &typecheck->error_type;
      }

      ast->ty = ast->expr.assign.expr->ty;
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_REF: {
      compiler_log(typecheck->compiler, LogLevelDebug, "typecheck", "typechecking ref");

      struct ast_expr *expr = ast->expr.ref.expr;

      {
        struct ast_ty *expr_ty = typecheck_expr(typecheck, expr, NULL);
        if (!expr_ty) {
          return NULL;
        }
      }

      if (expr->type != AST_EXPR_TYPE_VARIABLE) {
        fprintf(stderr, "ref expression must resolve to an identifier\n");
        ++typecheck->errors;
        return &typecheck->error_type;
      }

      const char *ident = expr->expr.variable.ident.value.identv.ident;

      struct scope_entry *entry = scope_lookup(typecheck->scope, ident, 1);
      if (!entry || entry->fdecl) {
        fprintf(stderr, "in ref, %s not found or not a variable\n", ident);
        ++typecheck->errors;
        return &typecheck->error_type;
      }

      struct ast_ty wrapped;
      memset(&wrapped, 0, sizeof(wrapped));
      wrapped.ty = AST_TYPE_POINTER;
      wrapped.oneof.pointer.pointee = expr->ty;
      ast->ty = type_repository_lookup_ty(typecheck->type_repo, &wrapped);
      if (!ast->ty) {
        ast->ty = type_repository_register(typecheck->type_repo, &wrapped);
      }
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_LOAD: {
      struct ast_expr *expr = ast->expr.load.expr;

      {
        struct ast_ty *expr_ty = typecheck_expr(typecheck, expr, expected_ty);
        if (!expr_ty) {
          return NULL;
        }
      }

      if (expr->ty->ty == AST_TYPE_POINTER) {
        ast->ty = type_repository_lookup_ty(typecheck->type_repo, ptr_pointee_type(expr->ty));
      } else if (expr->ty->ty == AST_TYPE_BOX) {
        fprintf(stderr, "use unbox instead of load to retrieve the interior value of a box\n");
        ++typecheck->errors;
        return &typecheck->error_type;
      } else {
        fprintf(stderr, "load expression must resolve to a pointer\n");
        ++typecheck->errors;
        return &typecheck->error_type;
      }

      return ast->ty;
    } break;

    case AST_EXPR_TYPE_UNARY: {
      {
        struct ast_ty *expr_ty = typecheck_expr(typecheck, ast->expr.unary.expr, expected_ty);
        if (!expr_ty) {
          return NULL;
        }
      }

      switch (ast->expr.unary.op) {
        case AST_UNARY_OP_NEG:
          if (ast->expr.unary.expr->ty->ty != AST_TYPE_INTEGER &&
              ast->expr.unary.expr->ty->ty != AST_TYPE_FLOAT) {
            fprintf(stderr, "negation expression must resolve to an integer or float\n");
            ++typecheck->errors;
            return &typecheck->error_type;
            ;
          }

          ast->ty = ast->expr.unary.expr->ty;
          return ast->ty;

        case AST_UNARY_OP_NOT:
          if (ast->expr.unary.expr->ty->ty != AST_TYPE_INTEGER) {
            fprintf(stderr, "not expression must resolve to an integer\n");
            ++typecheck->errors;
            return &typecheck->error_type;
            ;
          }

          ast->ty = ast->expr.unary.expr->ty;
          return ast->ty;

        case AST_UNARY_OP_COMP:
          if (ast->expr.unary.expr->ty->ty != AST_TYPE_INTEGER) {
            fprintf(stderr, "complement expression must resolve to an integer\n");
            ++typecheck->errors;
            return &typecheck->error_type;
            ;
          }

          ast->ty = ast->expr.unary.expr->ty;
          return ast->ty;

        default:
          fprintf(stderr, "unhandled unary op %d\n", ast->expr.unary.op);
          ++typecheck->errors;
          return &typecheck->error_type;
          ;
      }
    } break;

    case AST_EXPR_TYPE_MATCH: {
      {
        struct ast_ty *expr_ty = typecheck_expr(typecheck, ast->expr.match.expr, NULL);
        if (!expr_ty) {
          return NULL;
        }
      }

      // first pass: check that all arms have the same pattern type, and check their expressions
      struct ast_expr_match_arm *arm = ast->expr.match.arms;
      while (arm) {
        {
          struct ast_ty *pattern_ty =
              typecheck_pattern_match(typecheck, arm->pattern, &arm->pattern->expr.pattern_match,
                                      ast->expr.match.expr->ty, NULL);
          if (!pattern_ty) {
            return NULL;
          }
        }

        maybe_implicitly_convert(&arm->pattern->ty, &ast->expr.match.expr->ty);

        if (!same_type(arm->pattern->ty, ast->expr.match.expr->ty)) {
          char wantstr[256], gotstr[256];
          type_name_into(arm->pattern->ty, wantstr, 256);
          type_name_into(ast->expr.match.expr->ty, gotstr, 256);

          typecheck_diag_expr(typecheck, ast,
                              "match pattern has incorrect type, wanted %s but got %s\n", wantstr,
                              gotstr);
          ++typecheck->errors;
          return &typecheck->error_type;
          ;
        }

        struct scope_entry *inner_var = NULL;

        if (arm->pattern->type == AST_EXPR_TYPE_PATTERN_MATCH &&
            arm->pattern->expr.pattern_match.inner_vdecl) {
          typecheck->scope = enter_scope(typecheck->scope);

          struct scope_entry *entry = calloc(1, sizeof(struct scope_entry));
          entry->ty = arm->pattern->expr.pattern_match.inner_vdecl->ty;
          entry->decl_flags = arm->pattern->expr.pattern_match.inner_vdecl->flags;
          scope_insert(typecheck->scope,
                       arm->pattern->expr.pattern_match.inner_vdecl->ident.value.identv.ident,
                       entry);

          inner_var = entry;
        }

        struct ast_ty *arm_ty = typecheck_expr(typecheck, arm->expr, expected_ty);

        if (inner_var) {
          typecheck->scope = exit_scope(typecheck->scope);
        }

        if (!arm_ty) {
          return NULL;
        }

        arm = arm->next;
      }

      if (!ast->expr.match.otherwise) {
        typecheck_diag_expr(typecheck, ast, "match expression has no otherwise arm\n");
        ++typecheck->errors;
        return &typecheck->error_type;
      }

      struct ast_ty *otherwise_ty =
          typecheck_expr(typecheck, ast->expr.match.otherwise->expr, expected_ty);
      if (!otherwise_ty) {
        return NULL;
      }

      struct ast_ty *largest_ty = otherwise_ty;

      // second pass: check that all arms have the same type
      arm = ast->expr.match.arms;
      while (arm) {
        struct ast_expr_match_arm *next = arm->next ? arm->next : ast->expr.match.otherwise;
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
      const char *ident = ast->expr.enum_init.enum_ty_name.value.identv.ident;
      struct ast_ty *enumty = type_repository_lookup(typecheck->type_repo, ident);
      if (!enumty) {
        typecheck_diag_expr(typecheck, ast, "enum type %s not found\n", ident);
        return &typecheck->error_type;
      }

      if (enumty->ty != AST_TYPE_ENUM) {
        typecheck_diag_expr(typecheck, ast, "type %s is not an enum\n",
                            ast->expr.enum_init.enum_ty_name.value.identv.ident);
        return &typecheck->error_type;
      }

      struct ast_enum_field *field = enumty->oneof.enumty.fields;
      while (field) {
        if (!strcmp(field->name, ast->expr.enum_init.enum_val_name.value.identv.ident)) {
          break;
        }
        field = field->next;
      }

      if (!field) {
        typecheck_diag_expr(typecheck, ast, "enum field %s not found in enum %s\n",
                            ast->expr.enum_init.enum_val_name.value.identv.ident,
                            ast->expr.enum_init.enum_ty_name.value.identv.ident);
        return &typecheck->error_type;
      }

      if (ast->expr.enum_init.inner) {
        if (!field->has_inner) {
          typecheck_diag_expr(typecheck, ast, "enum field %s does not have an inner\n",
                              field->name);
          return &typecheck->error_type;
        }

        // includes an inner, ensure it matches the enum field's type
        {
          struct ast_ty *inner_ty =
              typecheck_expr(typecheck, ast->expr.enum_init.inner, field->inner);
          if (!inner_ty) {
            return NULL;
          }
        }

        maybe_implicitly_convert(&ast->expr.enum_init.inner->ty, &field->inner);

        if (field->inner->ty != AST_TYPE_CUSTOM) {
          // can't check types if the inner is as yet unresolved
          if (!same_type(ast->expr.enum_init.inner->ty, field->inner)) {
            char innerstr[256], fieldstr[256];
            type_name_into(ast->expr.enum_init.inner->ty, innerstr, 256);
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
      if (ast->expr.sizeof_expr.expr) {
        // the parser can't distinguish between types and identifiers, so we need to do so here
        if (ast->expr.sizeof_expr.expr->type == AST_EXPR_TYPE_VARIABLE) {
          inner_ty = type_repository_lookup(
              typecheck->type_repo,
              ast->expr.sizeof_expr.expr->expr.variable.ident.value.identv.ident);
          if (inner_ty) {
            free_expr(typecheck->compiler, ast->expr.sizeof_expr.expr);
            ast->expr.sizeof_expr.expr = NULL;
            swapped_type = 1;
          }
        }

        if (!swapped_type) {
          inner_ty = typecheck_expr(typecheck, ast->expr.sizeof_expr.expr, NULL);
          if (!inner_ty) {
            return NULL;
          }

          inner_ty = ast->expr.sizeof_expr.expr->ty;
        }
      } else {
        inner_ty = resolve_parsed_type(typecheck, &ast->expr.sizeof_expr.parsed_ty);
      }

      ast->expr.sizeof_expr.resolved = inner_ty;

      struct ast_ty itype;
      memset(&itype, 0, sizeof(itype));
      itype.ty = AST_TYPE_INTEGER;
      itype.oneof.integer.is_signed = 1;
      itype.oneof.integer.width = 32;
      ast->ty = resolve_type(typecheck, &itype);
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_BOX: {
      struct ast_ty *inner_ty = NULL;

      int swapped_type = 0;
      if (ast->expr.box_expr.expr) {
        // the parser can't distinguish between types and identifiers, so we need to do so here
        if (ast->expr.box_expr.expr->type == AST_EXPR_TYPE_VARIABLE) {
          inner_ty = type_repository_lookup(
              typecheck->type_repo,
              ast->expr.box_expr.expr->expr.variable.ident.value.identv.ident);
          if (inner_ty) {
            free_expr(typecheck->compiler, ast->expr.box_expr.expr);
            ast->expr.box_expr.expr = NULL;
            swapped_type = 1;
          }
        }

        if (!swapped_type) {
          inner_ty = typecheck_expr(typecheck, ast->expr.box_expr.expr, NULL);
          if (!inner_ty) {
            return NULL;
          }

          inner_ty = ast->expr.box_expr.expr->ty;
        }
      } else {
        inner_ty = resolve_parsed_type(typecheck, &ast->expr.box_expr.parsed_ty);
      }

      struct ast_ty box_ty;
      memset(&box_ty, 0, sizeof(box_ty));
      box_ty.ty = AST_TYPE_BOX;
      box_ty.oneof.pointer.pointee = inner_ty;
      ast->ty = type_repository_lookup_ty(typecheck->type_repo, &box_ty);
      if (!ast->ty) {
        ast->ty = type_repository_register(typecheck->type_repo, &box_ty);
      }
      return ast->ty;
    } break;

    case AST_EXPR_TYPE_UNBOX: {
      {
        struct ast_ty *inner_ty = typecheck_expr(typecheck, ast->expr.box_expr.expr, expected_ty);
        if (!inner_ty) {
          return NULL;
        }
      }

      if (ast->expr.box_expr.expr->ty->ty != AST_TYPE_BOX) {
        typecheck_diag_expr(typecheck, ast, "unbox can only be used with boxed types\n");
        return &typecheck->error_type;
      }

      // returns the underlying type of the box
      ast->ty = ast->expr.box_expr.expr->ty->oneof.pointer.pointee;
      return ast->ty;
    } break;

    default:
      typecheck_diag_expr(typecheck, ast, "typecheck: unhandled expression type %d\n", ast->type);
  }

  // all expressions must resolve to a type
  return &typecheck->void_type;
}

static struct ast_ty *typecheck_initializer(struct typecheck *typecheck, struct ast_expr *ast,
                                            struct ast_ty *expected_ty) {
  if (ast->type != AST_EXPR_TYPE_INITIALIZER) {
    return &typecheck->error_type;
  }

  struct ast_ty *result_ty = ast->ty ? ast->ty : expected_ty;
  if (!result_ty) {
    typecheck_diag_expr(typecheck, ast, "not enough information to infer type for initializer");
    return &typecheck->error_type;
  }

  ast->ty = result_ty;

  if (result_ty->ty == AST_TYPE_STRUCT) {
    struct ast_expr_list *field_expr = ast->expr.list;
    struct ast_struct_field *field = ast->ty->oneof.structty.fields;
    while (field_expr) {
      if (!field) {
        typecheck_diag_expr(typecheck, ast, "initializer has more elements than struct has fields");
        ++typecheck->errors;
        break;
      } else if (!field->ty) {
        typecheck_diag_expr(typecheck, ast, "struct field somehow has no resolved type");
        ++typecheck->errors;
        break;
      }

      struct ast_ty *ty = typecheck_expr(typecheck, field_expr->expr, field->ty);
      if (!ty) {
        return NULL;
      }

      maybe_implicitly_convert(&field_expr->expr->ty, &field->ty);

      if (!same_type(field_expr->expr->ty, field->ty)) {
        char exprty[256];
        char fieldty[256];
        type_name_into(field_expr->expr->ty, exprty, 256);
        type_name_into(field->ty, fieldty, 256);

        fprintf(stderr, "struct initializer field %s has type %s, expected %s\n", field->name,
                exprty, fieldty);
        ++typecheck->errors;
      }

      field_expr = field_expr->next;
      field = field->next;
    }

    return ast->ty;
  } else if (result_ty->ty == AST_TYPE_ARRAY) {
    struct ast_ty *element_expected_ty = result_ty->oneof.array.element_ty;
    struct ast_expr_list *node = ast->expr.list;
    while (node) {
      struct ast_ty *ty = typecheck_expr(typecheck, node->expr, element_expected_ty);
      if (!ty) {
        return NULL;
      }

      maybe_implicitly_convert(&node->expr->ty, &ast->ty->oneof.array.element_ty);

      node = node->next;
    }

    return ast->ty;
  } else {
    typecheck_diag_expr(typecheck, ast, "initializer provided for unknown type");
    return &typecheck->error_type;
  }
}

/*

    case AST_EXPR_TYPE_STRUCT_INIT: {
      ast->ty = resolve_parsed_type(typecheck, ast->parsed_ty.oneof.array.element_ty);
      if (type_is_tbd(ast->ty)) {
        ast->ty = expected_ty;
      }

      // the element_ty was just a carrier for the struct type, we can free it now
      free(ast->parsed_ty.oneof.array.element_ty);
      ast->parsed_ty.oneof.array.element_ty = NULL;

      struct ast_struct_field *field = ast->ty->oneof.structty.fields;
      struct ast_expr_list *node = ast->expr.list;
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
          struct ast_ty *expr_ty = typecheck_expr(typecheck, node->expr, field->ty);
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
     */
