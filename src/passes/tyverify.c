/**
 * Second typecheck pass - just verifies that the AST contains no TBD or ERROR types.
 */

#include <ctype.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "kv.h"
#include "scope.h"
#include "typecheck.h"
#include "types.h"
#include "utility.h"

int typecheck_verify_ast(struct ast_program *ast);
static int typecheck_verify_toplevel(struct ast_toplevel *ast);
static int typecheck_verify_block(struct ast_block *ast);
static int typecheck_verify_stmt(struct ast_stmt *ast);
static int typecheck_verify_expr(struct ast_expr *ast);
static int typecheck_verify_struct_decl(struct ast_ty *decl);

static int is_bad_type(struct ast_ty *ty) {
  // custom types should have been resolved by now
  return type_is_error(ty) || type_is_tbd(ty) || ty->ty == AST_TYPE_CUSTOM;
}

int typecheck_verify_ast(struct ast_program *ast) {
  struct ast_toplevel *decl = ast->decls;
  while (decl) {
    int rc = typecheck_verify_toplevel(decl);
    if (rc < 0) {
      return rc;
    }
    decl = decl->next;
  }

  return 0;
}

static int typecheck_verify_toplevel(struct ast_toplevel *ast) {
  if (ast->type == AST_DECL_TYPE_FDECL) {
    if (is_bad_type(&ast->fdecl.retty)) {
      fprintf(stderr, "function %s has unresolved return type\n",
              ast->fdecl.ident.value.identv.ident);
      return -1;
    }

    for (size_t i = 0; i < ast->fdecl.num_params; i++) {
      struct ast_ty *param_ty = &ast->fdecl.params[i]->ty;
      if (is_bad_type(param_ty)) {
        fprintf(stderr, "function %s has unresolved parameter type for parameter %zd\n",
                ast->fdecl.ident.value.identv.ident, i + 1);
        return -1;
      }
    }

    if (ast->fdecl.body) {
      if (typecheck_verify_block(ast->fdecl.body) < 0) {
        return -1;
      }
    }
  } else if (ast->type == AST_DECL_TYPE_VDECL) {
    if (is_bad_type(&ast->vdecl.ty)) {
      fprintf(stderr, "variable %s has unresolved type\n", ast->vdecl.ident.value.identv.ident);
      return -1;
    }

    if (ast->vdecl.init_expr) {
      if (typecheck_verify_expr(ast->vdecl.init_expr) < 0) {
        return -1;
      }
    }
  } else if (ast->type == AST_DECL_TYPE_TYDECL) {
    if (ast->tydecl.ty.ty == AST_TYPE_STRUCT) {
      return typecheck_verify_struct_decl(&ast->tydecl.ty);
    }
  }

  return 0;
}

static int typecheck_verify_struct_decl(struct ast_ty *decl) {
  struct ast_struct_field *field = decl->structty.fields;
  while (field) {
    if (is_bad_type(field->ty)) {
      fprintf(stderr, "struct %s field %s has unresolved type\n", decl->name, field->name);
      return -1;
    }

    field = field->next;
  }

  return 0;
}

static int typecheck_verify_block(struct ast_block *ast) {
  struct ast_stmt *stmt = ast->stmt;
  while (stmt) {
    if (typecheck_verify_stmt(stmt) < 0) {
      return -1;
    }
    stmt = stmt->next;
  }

  return 0;
}

static int typecheck_verify_stmt(struct ast_stmt *ast) {
  switch (ast->type) {
    case AST_STMT_TYPE_EXPR:
      return typecheck_verify_expr(ast->expr);

    case AST_STMT_TYPE_LET: {
      if (is_bad_type(&ast->let.ty)) {
        fprintf(stderr, "let %s has unresolved type\n", ast->let.ident.value.identv.ident);
        return -1;
      }

      return typecheck_verify_expr(ast->let.init_expr);
    } break;

    case AST_STMT_TYPE_ITER: {
      if (typecheck_verify_expr(ast->iter.range.start) < 0) {
        return -1;
      }
      if (typecheck_verify_expr(ast->iter.range.end) < 0) {
        return -1;
      }
      if (ast->iter.range.step) {
        if (typecheck_verify_expr(ast->iter.range.step) < 0) {
          return -1;
        }
      }

      return typecheck_verify_block(&ast->iter.block);
    } break;

    case AST_STMT_TYPE_STORE: {
      if (typecheck_verify_expr(ast->store.lhs) < 0) {
        return -1;
      }
      if (typecheck_verify_expr(ast->store.rhs) < 0) {
        return -1;
      }
    } break;

    case AST_STMT_TYPE_RETURN: {
      if (ast->expr) {
        return typecheck_verify_expr(ast->expr);
      }
    } break;

    case AST_STMT_TYPE_DEFER: {
      return typecheck_verify_expr(ast->expr);
    } break;

    case AST_STMT_TYPE_WHILE: {
      if (typecheck_verify_expr(ast->while_stmt.cond) < 0) {
        return -1;
      }

      return typecheck_verify_block(&ast->while_stmt.block);
    } break;

    case AST_STMT_TYPE_BREAK:
    case AST_STMT_TYPE_CONTINUE:
      break;

    default:
      fprintf(stderr, "tyverify: unhandled statement type %d\n", ast->type);
  }

  return 0;
}

static int typecheck_verify_expr(struct ast_expr *ast) {
  if (is_bad_type(&ast->ty)) {
    fprintf(stderr, "tyverify: expression node with type %d has unresolved type\n", ast->type);
    return -1;
  }

  switch (ast->type) {
    case AST_EXPR_TYPE_CONSTANT: {
      switch (ast->ty.ty) {
        case AST_TYPE_FVEC:
        case AST_TYPE_ARRAY: {
          struct ast_expr_list *node = ast->list;
          while (node) {
            if (typecheck_verify_expr(node->expr) < 0) {
              fprintf(stderr, "array/fvec initializer element has unresolved type\n");
              return -1;
            }
            node = node->next;
          }

        } break;
        default:
          break;
      }
    } break;

    case AST_EXPR_TYPE_STRUCT_INIT: {
      struct ast_expr_list *node = ast->list;
      while (node) {
        if (typecheck_verify_expr(node->expr) < 0) {
          return -1;
        }

        node = node->next;
      }
    } break;

    case AST_EXPR_TYPE_UNION_INIT:
      return typecheck_verify_expr(ast->union_init.inner);
      break;

    case AST_EXPR_TYPE_VARIABLE:
      break;

    case AST_EXPR_TYPE_ARRAY_INDEX:
      break;

    case AST_EXPR_TYPE_BINARY: {
      if (typecheck_verify_expr(ast->binary.lhs) < 0) {
        return -1;
      }
      if (typecheck_verify_expr(ast->binary.rhs) < 0) {
        return -1;
      }
    } break;

    case AST_EXPR_TYPE_BLOCK: {
      return typecheck_verify_block(&ast->block);
    } break;

    case AST_EXPR_TYPE_CALL: {
      struct ast_expr_list *args = ast->call.args;
      while (args) {
        if (typecheck_verify_expr(args->expr) < 0) {
          return -1;
        }

        args = args->next;
      }
    } break;

    case AST_EXPR_TYPE_DEREF:
      break;

    case AST_EXPR_TYPE_VOID:
      break;

    case AST_EXPR_TYPE_CAST: {
      if (typecheck_verify_expr(ast->cast.expr) < 0) {
        return -1;
      }
    } break;

    case AST_EXPR_TYPE_IF: {
      if (typecheck_verify_expr(ast->if_expr.cond) < 0) {
        return -1;
      }
      if (typecheck_verify_block(&ast->if_expr.then_block) < 0) {
        return -1;
      }
      if (ast->if_expr.elseifs) {
        struct ast_expr_elseif *elseif = ast->if_expr.elseifs;
        while (elseif) {
          if (typecheck_verify_expr(elseif->cond) < 0) {
            return -1;
          }
          if (typecheck_verify_block(&elseif->block) < 0) {
            return -1;
          }
          elseif = elseif->next;
        }
      }
      if (typecheck_verify_block(&ast->if_expr.else_block) < 0) {
        return -1;
      }
    } break;

    case AST_EXPR_TYPE_ASSIGN: {
      if (typecheck_verify_expr(ast->assign.expr) < 0) {
        return -1;
      }
    } break;

    case AST_EXPR_TYPE_REF:
      break;

    case AST_EXPR_TYPE_LOAD: {
      if (typecheck_verify_expr(ast->load.expr) < 0) {
        return -1;
      }
    } break;

    case AST_EXPR_TYPE_UNARY: {
      if (typecheck_verify_expr(ast->unary.expr) < 0) {
        return -1;
      }
    } break;

    case AST_EXPR_TYPE_MATCH: {
      if (typecheck_verify_expr(ast->match.expr) < 0) {
        return -1;
      }

      // first pass: check that all arms have the same pattern type, and check their expressions
      struct ast_expr_match_arm *arm = ast->match.arms;
      while (arm) {
        if (typecheck_verify_expr(arm->pattern) < 0) {
          return -1;
        }

        if (typecheck_verify_expr(arm->expr) < 0) {
          return -1;
        }

        arm = arm->next;
      }

      if (typecheck_verify_expr(ast->match.otherwise->expr) < 0) {
        return -1;
      }
    } break;

    case AST_EXPR_TYPE_NIL:
      break;

    case AST_EXPR_TYPE_PATTERN_MATCH:
      if (ast->pattern_match.inner_vdecl) {
        if (is_bad_type(&ast->pattern_match.inner_vdecl->ty)) {
          fprintf(stderr, "pattern match inner vdecl has unresolved type\n");
          return -1;
        }
      }
      break;

    case AST_EXPR_TYPE_ENUM_INIT: {
      if (ast->enum_init.inner) {
        if (typecheck_verify_expr(ast->enum_init.inner) < 0) {
          return -1;
        }

        // final sanity check of all field types
        struct ast_enum_field *field = ast->ty.enumty.fields;
        while (field) {
          if (!strcmp(ast->enum_init.enum_val_name.value.identv.ident, field->name)) {
            break;
          }

          field = field->next;
        }

        if (!field) {
          fprintf(stderr, "tyverify: enum field %s not found in enum %s\n",
                  ast->enum_init.enum_val_name.value.identv.ident,
                  ast->enum_init.enum_ty_name.value.identv.ident);
          return -1;
        }

        if (!field->has_inner) {
          fprintf(stderr, "tyverify: enum field %s does not have an inner\n", field->name);
          return -1;
        }

        if (!same_type(&field->inner, &ast->enum_init.inner->ty)) {
          char innerstr[256], fieldstr[256];
          type_name_into(&ast->enum_init.inner->ty, innerstr, 256);
          type_name_into(&field->inner, fieldstr, 256);
          fprintf(stderr,
                  "tyverify: enum field %s inner type does not match: wanted %s but got %s\n",
                  field->name, fieldstr, innerstr);
          return -1;
        }
      }
    } break;

    case AST_EXPR_TYPE_SIZEOF: {
      if (ast->sizeof_expr.expr) {
        if (typecheck_verify_expr(ast->sizeof_expr.expr) < 0) {
          return -1;
        }
      } else {
        if (is_bad_type(&ast->sizeof_expr.ty)) {
          fprintf(stderr, "tyverify: sizeof expression has unresolved type\n");
          return -1;
        }
      }
    } break;

    case AST_EXPR_TYPE_BOX:
    case AST_EXPR_TYPE_UNBOX:
      return typecheck_verify_expr(ast->box_expr.expr);
      break;

    default:
      fprintf(stderr, "tyverify: unhandled expression type %d\n", ast->type);
      return -1;
  }

  return 0;
}
