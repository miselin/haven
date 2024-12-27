#include "cfold.h"

#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "tokens.h"
#include "types.h"

struct cfolder {
  struct ast_program *ast;
  struct compiler *compiler;
};

static void cfold_toplevel(struct ast_toplevel *ast);
static void cfold_block(struct ast_block *ast);
static void cfold_stmt(struct ast_stmt *ast);
static struct ast_expr *cfold_expr(struct ast_expr *ast);

static struct ast_expr *cfold_unary(struct ast_expr *unary);

struct cfolder *new_cfolder(struct ast_program *ast, struct compiler *compiler) {
  struct cfolder *result = calloc(1, sizeof(struct cfolder));
  result->ast = ast;
  result->compiler = compiler;
  return result;
}

int cfolder_run(struct cfolder *cfolder) {
  struct ast_toplevel *decl = cfolder->ast->decls;
  while (decl) {
    cfold_toplevel(decl);
    decl = decl->next;
  }

  return 0;
}

void destroy_cfolder(struct cfolder *cfolder) {
  free(cfolder);
}

static void cfold_toplevel(struct ast_toplevel *ast) {
  if (ast->type == AST_DECL_TYPE_FDECL) {
    if (ast->fdecl.body) {
      cfold_block(ast->fdecl.body);
    }
  } else if (ast->type == AST_DECL_TYPE_VDECL) {
    if (ast->vdecl.init_expr) {
      ast->vdecl.init_expr = cfold_expr(ast->vdecl.init_expr);
    }
  }
}

static void cfold_block(struct ast_block *ast) {
  struct ast_stmt *stmt = ast->stmt;
  while (stmt) {
    cfold_stmt(stmt);
    stmt = stmt->next;
  }
}

static void cfold_stmt(struct ast_stmt *ast) {
  switch (ast->type) {
    case AST_STMT_TYPE_EXPR:
      ast->expr = cfold_expr(ast->expr);
      break;

    case AST_STMT_TYPE_LET:
      if (ast->let.init_expr) {
        ast->let.init_expr = cfold_expr(ast->let.init_expr);
      }
      break;

    case AST_STMT_TYPE_ITER:
      ast->iter.range.start = cfold_expr(ast->iter.range.start);
      ast->iter.range.end = cfold_expr(ast->iter.range.end);
      if (ast->iter.range.step) {
        ast->iter.range.step = cfold_expr(ast->iter.range.step);
      }
      cfold_block(&ast->iter.block);
      break;

    case AST_STMT_TYPE_STORE:
      ast->store.lhs = cfold_expr(ast->store.lhs);
      ast->store.rhs = cfold_expr(ast->store.rhs);
      break;

    case AST_STMT_TYPE_RETURN:
      if (ast->expr) {
        ast->expr = cfold_expr(ast->expr);
      }
      break;

    case AST_STMT_TYPE_DEFER:
      ast->expr = cfold_expr(ast->expr);
      break;

    case AST_STMT_TYPE_WHILE:
      ast->while_stmt.cond = cfold_expr(ast->while_stmt.cond);
      cfold_block(&ast->while_stmt.block);
      break;

    case AST_STMT_TYPE_BREAK:
    case AST_STMT_TYPE_CONTINUE:
      break;

    default:
      fprintf(stderr, "cfold: unhandled statement type %d\n", ast->type);
  }
}

static struct ast_expr *cfold_expr(struct ast_expr *ast) {
  switch (ast->type) {
    case AST_EXPR_TYPE_CONSTANT:
      switch (ast->ty.ty) {
        case AST_TYPE_FVEC:
        case AST_TYPE_ARRAY: {
          struct ast_expr_list *node = ast->list;
          while (node) {
            node->expr = cfold_expr(node->expr);
            node = node->next;
          }
        } break;

        default:
          break;
      }
      break;

    case AST_EXPR_TYPE_BINARY: {
      ast->binary.lhs = cfold_expr(ast->binary.lhs);
      ast->binary.rhs = cfold_expr(ast->binary.rhs);

      // TODO: if both are constants, fold them
    } break;

    case AST_EXPR_TYPE_BLOCK: {
      cfold_block(&ast->block);
    } break;

    case AST_EXPR_TYPE_CALL: {
      struct ast_expr_list *args = ast->call.args;
      while (args) {
        args->expr = cfold_expr(args->expr);
        args = args->next;
      }
    } break;

    case AST_EXPR_TYPE_CAST: {
      ast->cast.expr = cfold_expr(ast->cast.expr);
    } break;

    case AST_EXPR_TYPE_UNARY: {
      ast = cfold_unary(ast);
    } break;

    case AST_EXPR_TYPE_IF: {
      ast->if_expr.cond = cfold_expr(ast->if_expr.cond);
      cfold_block(&ast->if_expr.then_block);
      if (ast->if_expr.has_else) {
        cfold_block(&ast->if_expr.else_block);
      }
    } break;

    case AST_EXPR_TYPE_ASSIGN: {
      ast->assign.expr = cfold_expr(ast->assign.expr);
    } break;

    case AST_EXPR_TYPE_REF: {
      ast->ref.expr = cfold_expr(ast->ref.expr);
    } break;

    case AST_EXPR_TYPE_LOAD: {
      ast->load.expr = cfold_expr(ast->load.expr);
    } break;

    case AST_EXPR_TYPE_ARRAY_INDEX: {
      ast->array_index.index = cfold_expr(ast->array_index.index);
    } break;
  }

  return ast;
}

static struct ast_expr *cfold_unary(struct ast_expr *unary) {
  struct ast_expr *new_expr = calloc(1, sizeof(struct ast_expr));
  int folded = 0;

  unary->unary.expr = cfold_expr(unary->unary.expr);
  struct ast_expr *inner = unary->unary.expr;

  if (inner->type != AST_EXPR_TYPE_CONSTANT) {
    free(new_expr);
    return unary;
  }

  memcpy(new_expr, inner, sizeof(struct ast_expr));

  switch (unary->unary.op) {
    case AST_UNARY_OP_NEG:
      switch (inner->ty.ty) {
        case AST_TYPE_INTEGER: {
          new_expr->constant.constant.value.intv.val = -inner->constant.constant.value.intv.val;
          folded = 1;
        } break;
        case AST_TYPE_FLOAT: {
          char *old_buf = inner->constant.constant.value.floatv.buf;
          char *new_buf = new_expr->constant.constant.value.floatv.buf;

          if (strlen(old_buf) >= 255) {
            // constant is too long, don't bother
            break;
          }

          if (old_buf[0] == '-') {
            strcpy(new_buf, old_buf + 1);
          } else {
            new_buf[0] = '-';
            strncpy(new_buf + 1, old_buf, 255);
          }
          folded = 1;
        } break;

        default:
          fprintf(stderr, "unhandled constant type %d\n", inner->constant.constant.ident);
      }
      break;
  }

  if (folded) {
    free_expr(unary);
    return new_expr;
  }

  free_expr(new_expr);
  return unary;
}
