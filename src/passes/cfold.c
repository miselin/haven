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

static void cfold_program(struct cfolder *cfolder, struct ast_program *ast);
static void cfold_toplevel(struct cfolder *cfolder, struct ast_toplevel *ast);
static void cfold_block(struct cfolder *cfolder, struct ast_block *ast);
static void cfold_stmt(struct cfolder *cfolder, struct ast_stmt *ast);
static struct ast_expr *cfold_expr(struct cfolder *cfolder, struct ast_expr *ast);

static struct ast_expr *cfold_unary(struct cfolder *cfolder, struct ast_expr *unary);

struct cfolder *new_cfolder(struct ast_program *ast, struct compiler *compiler) {
  struct cfolder *result = calloc(1, sizeof(struct cfolder));
  result->ast = ast;
  result->compiler = compiler;
  return result;
}

int cfolder_run(struct cfolder *cfolder) {
  cfold_program(cfolder, cfolder->ast);
  return 0;
}

static void cfold_program(struct cfolder *cfolder, struct ast_program *ast) {
  struct ast_toplevel *decl = ast->decls;
  while (decl) {
    cfold_toplevel(cfolder, decl);
    decl = decl->next;
  }
}

void destroy_cfolder(struct cfolder *cfolder) {
  free(cfolder);
}

static void cfold_toplevel(struct cfolder *cfolder, struct ast_toplevel *ast) {
  if (ast->type == AST_DECL_TYPE_FDECL) {
    if (ast->toplevel.fdecl.body) {
      cfold_block(cfolder, ast->toplevel.fdecl.body);
    }
  } else if (ast->type == AST_DECL_TYPE_VDECL) {
    if (ast->toplevel.vdecl.init_expr) {
      ast->toplevel.vdecl.init_expr = cfold_expr(cfolder, ast->toplevel.vdecl.init_expr);
    }
  } else if (ast->type == AST_DECL_TYPE_IMPORT && ast->toplevel.import.ast) {
    cfold_program(cfolder, ast->toplevel.import.ast);
  }
}

static void cfold_block(struct cfolder *cfolder, struct ast_block *ast) {
  struct ast_stmt *stmt = ast->stmt;
  while (stmt) {
    cfold_stmt(cfolder, stmt);
    stmt = stmt->next;
  }
}

static void cfold_stmt(struct cfolder *cfolder, struct ast_stmt *ast) {
  switch (ast->type) {
    case AST_STMT_TYPE_EXPR:
      ast->stmt.expr = cfold_expr(cfolder, ast->stmt.expr);
      break;

    case AST_STMT_TYPE_LET:
      if (ast->stmt.let.init_expr) {
        ast->stmt.let.init_expr = cfold_expr(cfolder, ast->stmt.let.init_expr);
      }
      break;

    case AST_STMT_TYPE_ITER:
      ast->stmt.iter.range.start = cfold_expr(cfolder, ast->stmt.iter.range.start);
      ast->stmt.iter.range.end = cfold_expr(cfolder, ast->stmt.iter.range.end);
      if (ast->stmt.iter.range.step) {
        ast->stmt.iter.range.step = cfold_expr(cfolder, ast->stmt.iter.range.step);
      }
      cfold_block(cfolder, &ast->stmt.iter.block);
      break;

    case AST_STMT_TYPE_STORE:
      ast->stmt.store.lhs = cfold_expr(cfolder, ast->stmt.store.lhs);
      ast->stmt.store.rhs = cfold_expr(cfolder, ast->stmt.store.rhs);
      break;

    case AST_STMT_TYPE_RETURN:
      if (ast->stmt.expr) {
        ast->stmt.expr = cfold_expr(cfolder, ast->stmt.expr);
      }
      break;

    case AST_STMT_TYPE_DEFER:
      ast->stmt.expr = cfold_expr(cfolder, ast->stmt.expr);
      break;

    case AST_STMT_TYPE_WHILE:
      ast->stmt.while_stmt.cond = cfold_expr(cfolder, ast->stmt.while_stmt.cond);
      cfold_block(cfolder, &ast->stmt.while_stmt.block);
      break;

    case AST_STMT_TYPE_BREAK:
    case AST_STMT_TYPE_CONTINUE:
      break;

    default:
      fprintf(stderr, "cfold: unhandled statement type %d\n", ast->type);
  }
}

static struct ast_expr *cfold_expr(struct cfolder *cfolder, struct ast_expr *ast) {
  switch (ast->type) {
    case AST_EXPR_TYPE_CONSTANT:
      switch (ast->parsed_ty.ty) {
        case AST_TYPE_FVEC:
        case AST_TYPE_ARRAY: {
          struct ast_expr_list *node = ast->expr.list;
          while (node) {
            node->expr = cfold_expr(cfolder, node->expr);
            node = node->next;
          }
        } break;

        default:
          break;
      }
      break;

    case AST_EXPR_TYPE_BINARY: {
      ast->expr.binary.lhs = cfold_expr(cfolder, ast->expr.binary.lhs);
      ast->expr.binary.rhs = cfold_expr(cfolder, ast->expr.binary.rhs);

      // TODO: if both are constants, fold them
    } break;

    case AST_EXPR_TYPE_BLOCK: {
      cfold_block(cfolder, &ast->expr.block);
    } break;

    case AST_EXPR_TYPE_CALL: {
      struct ast_expr_list *args = ast->expr.call.args;
      while (args) {
        args->expr = cfold_expr(cfolder, args->expr);
        args = args->next;
      }
    } break;

    case AST_EXPR_TYPE_CAST: {
      ast->expr.cast.expr = cfold_expr(cfolder, ast->expr.cast.expr);
    } break;

    case AST_EXPR_TYPE_UNARY: {
      ast = cfold_unary(cfolder, ast);
    } break;

    case AST_EXPR_TYPE_IF: {
      ast->expr.if_expr.cond = cfold_expr(cfolder, ast->expr.if_expr.cond);
      cfold_block(cfolder, &ast->expr.if_expr.then_block);
      if (ast->expr.if_expr.has_else) {
        cfold_block(cfolder, &ast->expr.if_expr.else_block);
      }
    } break;

    case AST_EXPR_TYPE_ASSIGN: {
      ast->expr.assign.expr = cfold_expr(cfolder, ast->expr.assign.expr);
    } break;

    case AST_EXPR_TYPE_REF: {
      ast->expr.ref.expr = cfold_expr(cfolder, ast->expr.ref.expr);
    } break;

    case AST_EXPR_TYPE_LOAD: {
      ast->expr.load.expr = cfold_expr(cfolder, ast->expr.load.expr);
    } break;

    case AST_EXPR_TYPE_ARRAY_INDEX: {
      ast->expr.array_index.index = cfold_expr(cfolder, ast->expr.array_index.index);
    } break;
  }

  return ast;
}

static struct ast_expr *cfold_unary(struct cfolder *cfolder, struct ast_expr *unary) {
  struct ast_expr *new_expr = calloc(1, sizeof(struct ast_expr));
  int folded = 0;

  unary->expr.unary.expr = cfold_expr(cfolder, unary->expr.unary.expr);
  struct ast_expr *inner = unary->expr.unary.expr;

  if (inner->type != AST_EXPR_TYPE_CONSTANT) {
    free(new_expr);
    return unary;
  }

  memcpy(new_expr, inner, sizeof(struct ast_expr));

  switch (unary->expr.unary.op) {
    case AST_UNARY_OP_NEG:
      switch (inner->parsed_ty.ty) {
        case AST_TYPE_INTEGER: {
          new_expr->expr.constant.constant.value.intv.val =
              -inner->expr.constant.constant.value.intv.val;
          folded = 1;
        } break;
        case AST_TYPE_FLOAT: {
          char *old_buf = inner->expr.constant.constant.value.floatv.buf;
          char *new_buf = new_expr->expr.constant.constant.value.floatv.buf;

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
          fprintf(stderr, "unhandled constant type %d\n", inner->expr.constant.constant.ident);
      }
      break;
  }

  if (folded) {
    free_expr(cfolder->compiler, unary);
    return new_expr;
  }

  free_expr(cfolder->compiler, new_expr);
  return unary;
}
