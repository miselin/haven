/**
 * Third typecheck pass - with all types resolved, finds more implicit conversions to make.
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

// Returns 1 if an implicit conversion was made. Returns 0 if none were made.
// Run until no more implicit conversions can be made.
int typecheck_implicit_ast(struct ast_program *ast);
static int typecheck_implicit_toplevel(struct ast_toplevel *ast);
static int typecheck_implicit_block(struct ast_block *ast);
static int typecheck_implicit_stmt(struct ast_stmt *ast);
static int typecheck_implicit_expr(struct ast_expr *ast);
static int typecheck_implicit_struct_decl(struct ast_ty *decl);

int maybe_implicitly_convert(struct ast_ty *from, struct ast_ty *to);

static int is_bad_type(struct ast_ty *ty) {
  // custom types should have been resolved by now
  return type_is_error(ty) || type_is_tbd(ty) || ty->ty == AST_TYPE_CUSTOM;
}

int typecheck_implicit_ast(struct ast_program *ast) {
  struct ast_toplevel *decl = ast->decls;
  int total = 0;
  while (decl) {
    int rc = typecheck_implicit_toplevel(decl);
    if (rc < 0) {
      return rc;
    }
    total += rc;
    decl = decl->next;
  }

  return total;
}

static int typecheck_implicit_toplevel(struct ast_toplevel *ast) {
  int total = 0;

  if (ast->type == AST_DECL_TYPE_FDECL) {
    if (ast->fdecl.body) {
      int rc = typecheck_implicit_block(ast->fdecl.body);
      if (rc < 0) {
        return -1;
      }
      total += rc;
    }
  } else if (ast->type == AST_DECL_TYPE_VDECL) {
    if (ast->vdecl.init_expr) {
      int rc = typecheck_implicit_expr(ast->vdecl.init_expr);
      if (rc < 0) {
        return -1;
      }
      total += rc;
    }
  } else if (ast->type == AST_DECL_TYPE_TYDECL) {
    if (ast->tydecl.ty.ty == AST_TYPE_STRUCT) {
      return typecheck_implicit_struct_decl(&ast->tydecl.ty);
    }
  }

  return total;
}

static int typecheck_implicit_struct_decl(struct ast_ty *decl) {
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

static int typecheck_implicit_block(struct ast_block *ast) {
  struct ast_stmt *stmt = ast->stmt;
  int total = 0;
  while (stmt) {
    int rc = typecheck_implicit_stmt(stmt);
    if (rc < 0) {
      return -1;
    }
    total += rc;
    stmt = stmt->next;
  }

  return total;
}

static int typecheck_implicit_stmt(struct ast_stmt *ast) {
  switch (ast->type) {
    case AST_STMT_TYPE_EXPR:
      return typecheck_implicit_expr(ast->expr);

    case AST_STMT_TYPE_LET: {
      return typecheck_implicit_expr(ast->let.init_expr);
    } break;

    case AST_STMT_TYPE_ITER: {
      int total = 0;
      int rc = typecheck_implicit_expr(ast->iter.range.start);
      if (rc < 0) {
        return -1;
      }
      total += rc;

      rc = typecheck_implicit_expr(ast->iter.range.end);
      if (rc < 0) {
        return -1;
      }

      if (ast->iter.range.step) {
        rc = typecheck_implicit_expr(ast->iter.range.step);
        if (rc < 0) {
          return -1;
        }

        total += rc;
      }

      return total + typecheck_implicit_block(&ast->iter.block);
    } break;

    case AST_STMT_TYPE_STORE: {
      int a = typecheck_implicit_expr(ast->store.lhs);
      if (a < 0) {
        return -1;
      }
      int b = typecheck_implicit_expr(ast->store.rhs);
      if (b < 0) {
        return -1;
      }
      return a + b;
    } break;

    case AST_STMT_TYPE_RETURN: {
      return typecheck_implicit_expr(ast->expr);
    } break;

    case AST_STMT_TYPE_DEFER: {
      return typecheck_implicit_expr(ast->expr);
    } break;

    default:
      fprintf(stderr, "tyverify: unhandled statement type %d\n", ast->type);
  }

  return 0;
}

static int typecheck_implicit_expr(struct ast_expr *ast) {
  switch (ast->type) {
    case AST_EXPR_TYPE_CONSTANT: {
      switch (ast->ty.ty) {
        case AST_TYPE_FVEC:
        case AST_TYPE_ARRAY: {
          struct ast_expr_list *node = ast->list;
          int total = 0;
          while (node) {
            int rc = typecheck_implicit_expr(node->expr);
            if (rc < 0) {
              return -1;
            }
            total += rc;
            node = node->next;
          }

          return total;
        } break;
        default:
          break;
      }
    } break;

    case AST_EXPR_TYPE_STRUCT_INIT: {
      struct ast_expr_list *node = ast->list;
      int total = 0;
      while (node) {
        int rc = typecheck_implicit_expr(node->expr);
        if (rc < 0) {
          return -1;
        }

        total += rc;

        node = node->next;
      }

      return total;
    } break;

    case AST_EXPR_TYPE_VARIABLE:
      break;

    case AST_EXPR_TYPE_ARRAY_INDEX:
      break;

    case AST_EXPR_TYPE_BINARY: {
      int total = maybe_implicitly_convert(&ast->binary.lhs->ty, &ast->ty) +
                  maybe_implicitly_convert(&ast->binary.rhs->ty, &ast->ty);
      int left = typecheck_implicit_expr(ast->binary.lhs);
      int right = typecheck_implicit_expr(ast->binary.rhs);
      if (left < 0 || right < 0) {
        return -1;
      }

      return total + left + right;
    } break;

    case AST_EXPR_TYPE_LOGICAL: {
      int total = maybe_implicitly_convert(&ast->binary.lhs->ty, &ast->ty) +
                  maybe_implicitly_convert(&ast->binary.rhs->ty, &ast->ty);
      int left = typecheck_implicit_expr(ast->binary.lhs);
      int right = typecheck_implicit_expr(ast->binary.rhs);
      if (left < 0 || right < 0) {
        return -1;
      }

      return total + left + right;
    } break;

    case AST_EXPR_TYPE_BLOCK: {
      return typecheck_implicit_block(&ast->block);
    } break;

    case AST_EXPR_TYPE_CALL: {
      struct ast_expr_list *args = ast->call.args;
      int total = 0;
      while (args) {
        int rc = typecheck_implicit_expr(args->expr);
        if (rc < 0) {
          return -1;
        }
        total += rc;

        args = args->next;
      }
      return total;
    } break;

    case AST_EXPR_TYPE_DEREF:
      break;

    case AST_EXPR_TYPE_VOID:
      break;

    case AST_EXPR_TYPE_CAST: {
      int total = maybe_implicitly_convert(&ast->cast.expr->ty, &ast->ty);
      int rc = typecheck_implicit_expr(ast->cast.expr);
      if (rc < 0) {
        return -1;
      }
      return total + rc;
    } break;

    case AST_EXPR_TYPE_IF: {
      int total = 0;
      int rc = typecheck_implicit_expr(ast->if_expr.cond);
      if (rc < 0) {
        return -1;
      }
      total += rc;

      rc = typecheck_implicit_block(&ast->if_expr.then_block);
      if (rc < 0) {
        return -1;
      }
      total += rc;

      rc = typecheck_implicit_block(&ast->if_expr.else_block);
      if (rc < 0) {
        return -1;
      }

      return total + rc;
    } break;

    case AST_EXPR_TYPE_ASSIGN: {
      return typecheck_implicit_expr(ast->assign.expr);
    } break;

    case AST_EXPR_TYPE_REF:
      break;

    case AST_EXPR_TYPE_LOAD: {
      return typecheck_implicit_expr(ast->load.expr);
    } break;

    case AST_EXPR_TYPE_UNARY: {
      return typecheck_implicit_expr(ast->unary.expr);
    } break;

    case AST_EXPR_TYPE_BOOLEAN: {
      if (typecheck_implicit_expr(ast->boolean.lhs) < 0) {
        return -1;
      }
      if (typecheck_implicit_expr(ast->boolean.rhs) < 0) {
        return -1;
      }
    } break;

    case AST_EXPR_TYPE_MATCH: {
      if (typecheck_implicit_expr(ast->match.expr) < 0) {
        return -1;
      }

      // first pass: check that all arms have the same pattern type, and check their expressions
      struct ast_expr_match_arm *arm = ast->match.arms;
      while (arm) {
        if (typecheck_implicit_expr(arm->pattern) < 0) {
          return -1;
        }

        if (typecheck_implicit_expr(arm->expr) < 0) {
          return -1;
        }

        arm = arm->next;
      }

      if (typecheck_implicit_expr(ast->match.otherwise->expr) < 0) {
        return -1;
      }
    } break;

    case AST_EXPR_TYPE_NIL:
      break;

    default:
      fprintf(stderr, "tyverify: unhandled expression type %d\n", ast->type);
  }

  return 0;
}
