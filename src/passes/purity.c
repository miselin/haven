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

struct purity {
  struct ast_program *ast;
};

int check_purity_ast(struct ast_program *ast);
static int check_purity_toplevel(struct ast_toplevel *ast);
static int check_purity_block(struct ast_block *ast);
static int check_purity_stmt(struct ast_stmt *ast);
static int check_purity_expr(struct ast_expr *ast);
static int check_purity_struct_decl(struct ast_ty *decl);

struct purity *purity_new(struct ast_program *ast) {
  struct purity *purity = calloc(1, sizeof(struct purity));
  purity->ast = ast;
  return purity;
}

int purity_run(struct purity *purity) {
  return check_purity_ast(purity->ast);
}

void purity_destroy(struct purity *purity) {
  free(purity);
}

int check_purity_ast(struct ast_program *ast) {
  struct ast_toplevel *decl = ast->decls;
  int total = 0;
  while (decl) {
    int rc = check_purity_toplevel(decl);
    if (rc < 0) {
      return rc;
    }
    total += rc;
    decl = decl->next;
  }

  return total;
}

static int check_purity_toplevel(struct ast_toplevel *ast) {
  int total = 0;

  if (ast->type == AST_DECL_TYPE_FDECL) {
    if (ast->fdecl.flags & DECL_FLAG_IMPURE) {
      return 0;
    }

    if (ast->fdecl.body) {
      if (check_purity_block(ast->fdecl.body) < 0) {
        fprintf(stderr,
                "function %s is impure - it reads or writes memory, or calls an impure function\n",
                ast->fdecl.ident.value.identv.ident);
        return -1;
      }
    }
  }

  return 0;
}

static int check_purity_block(struct ast_block *ast) {
  struct ast_stmt *stmt = ast->stmt;
  while (stmt) {
    if (check_purity_stmt(stmt) < 0) {
      return -1;
    }
    stmt = stmt->next;
  }

  return 0;
}

static int check_purity_stmt(struct ast_stmt *ast) {
  switch (ast->type) {
    case AST_STMT_TYPE_EXPR:
      return check_purity_expr(ast->expr);

    case AST_STMT_TYPE_LET: {
      return check_purity_expr(ast->let.init_expr);
    } break;

    case AST_STMT_TYPE_ITER: {
      int total = 0;
      int rc = check_purity_expr(ast->iter.range.start);
      if (rc < 0) {
        return -1;
      }
      total += rc;

      rc = check_purity_expr(ast->iter.range.end);
      if (rc < 0) {
        return -1;
      }

      if (ast->iter.range.step) {
        rc = check_purity_expr(ast->iter.range.step);
        if (rc < 0) {
          return -1;
        }

        total += rc;
      }

      return check_purity_block(&ast->iter.block);
    } break;

    case AST_STMT_TYPE_STORE: {
      return -1;
    } break;

    case AST_STMT_TYPE_RETURN: {
      return check_purity_expr(ast->expr);
    } break;

    case AST_STMT_TYPE_DEFER: {
      return check_purity_expr(ast->expr);
    } break;

    default:
      fprintf(stderr, "purity: unhandled statement type %d\n", ast->type);
  }

  return 0;
}

static int check_purity_expr(struct ast_expr *ast) {
  switch (ast->type) {
    case AST_EXPR_TYPE_CONSTANT: {
      switch (ast->ty.ty) {
        case AST_TYPE_FVEC:
        case AST_TYPE_ARRAY: {
          struct ast_expr_list *node = ast->list;
          int total = 0;
          while (node) {
            int rc = check_purity_expr(node->expr);
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
        int rc = check_purity_expr(node->expr);
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
      if (check_purity_expr(ast->binary.lhs) < 0) {
        return -1;
      }
      if (check_purity_expr(ast->binary.rhs) < 0) {
        return -1;
      }
    } break;

    case AST_EXPR_TYPE_LOGICAL: {
      if (check_purity_expr(ast->binary.lhs) < 0) {
        return -1;
      }
      if (check_purity_expr(ast->binary.rhs) < 0) {
        return -1;
      }
    } break;

    case AST_EXPR_TYPE_BLOCK: {
      return check_purity_block(&ast->block);
    } break;

    case AST_EXPR_TYPE_CALL: {
      if (ast->call.fdecl->flags & DECL_FLAG_IMPURE) {
        // cannot call impure functions from pure functions
        return -1;
      }

      struct ast_expr_list *args = ast->call.args;
      while (args) {
        if (check_purity_expr(args->expr) < 0) {
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
      return check_purity_expr(ast->cast.expr);
    } break;

    case AST_EXPR_TYPE_IF: {
      if (check_purity_expr(ast->if_expr.cond) < 0) {
        return -1;
      }

      if (check_purity_block(&ast->if_expr.then_block) < 0) {
        return -1;
      }

      if (check_purity_block(&ast->if_expr.else_block) < 0) {
        return -1;
      }
    } break;

    case AST_EXPR_TYPE_ASSIGN: {
      return check_purity_expr(ast->assign.expr);
    } break;

    case AST_EXPR_TYPE_REF:
      break;

    case AST_EXPR_TYPE_LOAD: {
      return -1;
    } break;

    case AST_EXPR_TYPE_UNARY: {
      return check_purity_expr(ast->unary.expr);
    } break;

    case AST_EXPR_TYPE_BOOLEAN: {
      if (check_purity_expr(ast->boolean.lhs) < 0) {
        return -1;
      }
      if (check_purity_expr(ast->boolean.rhs) < 0) {
        return -1;
      }
    } break;

    case AST_EXPR_TYPE_MATCH: {
      if (check_purity_expr(ast->match.expr) < 0) {
        return -1;
      }

      // first pass: check that all arms have the same pattern type, and check their expressions
      struct ast_expr_match_arm *arm = ast->match.arms;
      while (arm) {
        if (check_purity_expr(arm->pattern) < 0) {
          return -1;
        }

        if (check_purity_expr(arm->expr) < 0) {
          return -1;
        }

        arm = arm->next;
      }

      if (check_purity_expr(ast->match.otherwise->expr) < 0) {
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
