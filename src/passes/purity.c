#include <stdarg.h>
#include <stdlib.h>

#include "ast.h"
#include "types.h"

struct purity {
  struct ast_program *ast;
  struct compiler *compiler;
  struct ast_fdecl *current_function;
  int errors;
};

int check_purity_ast(struct ast_program *ast);
static int check_purity_expr(struct purity *purity, struct ast_expr *ast);

static enum VisitorResult purity_visitor(struct ast_visitor_node *node, void *user_data);

struct purity *purity_new(struct ast_program *ast, struct compiler *compiler) {
  struct purity *purity = calloc(1, sizeof(struct purity));
  purity->ast = ast;
  purity->compiler = compiler;
  return purity;
}

int purity_run(struct purity *purity) {
  ast_visit(purity->compiler, purity->ast, purity_visitor, purity);
  return purity->errors;
}

void purity_destroy(struct purity *purity) {
  free(purity);
}

static enum VisitorResult purity_visitor(struct ast_visitor_node *node, void *user_data) {
  struct purity *purity = user_data;

  if (node->toplevel) {
    if (node->toplevel->type == AST_DECL_TYPE_FDECL) {
      purity->current_function = &node->toplevel->toplevel.fdecl;
    }
  } else if (node->expr) {
    // no need to process expressions in explicitly marked impure functions
    if (purity->current_function && (purity->current_function->flags & DECL_FLAG_IMPURE) == 0) {
      check_purity_expr(purity, node->expr);
    }
  }

  return purity->errors ? VisitorStop : VisitorContinue;
}

static int check_purity_expr(struct purity *purity, struct ast_expr *ast) {
  switch (ast->type) {
    case AST_EXPR_TYPE_CONSTANT:
      break;

    case AST_EXPR_TYPE_STRUCT_INIT:
      break;

    case AST_EXPR_TYPE_VARIABLE:
      break;

    case AST_EXPR_TYPE_ARRAY_INDEX:
      break;

    case AST_EXPR_TYPE_BINARY:
      break;

    case AST_EXPR_TYPE_BLOCK:
      break;

    case AST_EXPR_TYPE_CALL: {
      if (!ast->expr.call.fdecl) {
        compiler_log(purity->compiler, LogLevelError, "purity",
                     "calling function pointer in %s is not allowed from pure functions",
                     ast->expr.call.ident.value.identv.ident);
        purity->errors++;
      } else if (ast->expr.call.fdecl->flags & DECL_FLAG_IMPURE) {
        compiler_log(purity->compiler, LogLevelError, "purity",
                     "call from pure function to impure function %s is not allowed",
                     ast->expr.call.ident.value.identv.ident);
        purity->errors++;
      }
    } break;

    case AST_EXPR_TYPE_DEREF:
      if (ast->expr.deref.is_ptr) {
        compiler_log(purity->compiler, LogLevelError, "purity",
                     "dereference of pointer is not allowed in pure functions");
        purity->errors++;
      }
      break;

    case AST_EXPR_TYPE_VOID:
      break;

    case AST_EXPR_TYPE_CAST:
      break;

    case AST_EXPR_TYPE_IF:
      break;

    case AST_EXPR_TYPE_ASSIGN:
      break;

    case AST_EXPR_TYPE_REF:
      break;

    case AST_EXPR_TYPE_LOAD:
      compiler_log(purity->compiler, LogLevelError, "purity",
                   "load is not allowed in pure functions");
      purity->errors++;
      break;

    case AST_EXPR_TYPE_UNARY:
      break;

    case AST_EXPR_TYPE_MATCH:
      break;

    case AST_EXPR_TYPE_NIL:
      break;

    case AST_EXPR_TYPE_ENUM_INIT:
      break;

    case AST_EXPR_TYPE_PATTERN_MATCH:
      break;

    case AST_EXPR_TYPE_SIZEOF:
      break;

    case AST_EXPR_TYPE_BOX:
    case AST_EXPR_TYPE_UNBOX:
      break;

    default:
      compiler_log(purity->compiler, LogLevelError, "purity", "unhandled expression type %d",
                   ast->type);
  }

  return 0;
}
