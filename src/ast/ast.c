#include "ast.h"

void ast_remove(struct compiler *compiler, struct ast_program *ast, struct ast_toplevel *node) {
  struct ast_toplevel *prev = NULL;
  struct ast_toplevel *decl = ast->decls;
  while (decl) {
    if (decl == node) {
      if (prev) {
        prev->next = decl->next;
      } else {
        ast->decls = decl->next;
      }
      free_toplevel(compiler, decl);
      return;
    }
    prev = decl;
    decl = decl->next;
  }
}

const char *ast_binary_op_to_str(int op) {
  switch (op) {
    case AST_BINARY_OP_ADD:
      return "+";
    case AST_BINARY_OP_SUB:
      return "-";
    case AST_BINARY_OP_MUL:
      return "*";
    case AST_BINARY_OP_DIV:
      return "/";
    case AST_BINARY_OP_MOD:
      return "%";
    case AST_BINARY_OP_BITAND:
      return "&";
    case AST_BINARY_OP_BITOR:
      return "|";
    case AST_BINARY_OP_BITXOR:
      return "^";
    case AST_BINARY_OP_LSHIFT:
      return "<<";
    case AST_BINARY_OP_RSHIFT:
      return ">>";
    case AST_BINARY_OP_LOGICAL_OR:
      return "||";
    case AST_BINARY_OP_LOGICAL_AND:
      return "&&";
    case AST_BINARY_OP_EQUAL:
      return "==";
    case AST_BINARY_OP_NOT_EQUAL:
      return "!=";
    case AST_BINARY_OP_LT:
      return "<";
    case AST_BINARY_OP_LTE:
      return "<=";
    case AST_BINARY_OP_GT:
      return ">";
    case AST_BINARY_OP_GTE:
      return ">=";
    default:
      return "<unknown-binary-op>";
  }
}
const char *ast_unary_op_to_str(int op) {
  switch (op) {
    case AST_UNARY_OP_NEG:
      return "-";
    case AST_UNARY_OP_NOT:
      return "!";
    case AST_UNARY_OP_COMP:
      return "~";
    default:
      return "<unknown-unary-op>";
  }
}

const char *ast_logical_op_to_str(int op) {
  switch (op) {
    case AST_LOGICAL_OP_OR:
      return "||";
    case AST_LOGICAL_OP_AND:
      return "&&";
    default:
      return "<unknown-logical-op>";
  }
}

int ast_binary_op_conditional(int op) {
  switch (op) {
    case AST_BINARY_OP_EQUAL:
    case AST_BINARY_OP_NOT_EQUAL:
    case AST_BINARY_OP_LT:
    case AST_BINARY_OP_LTE:
    case AST_BINARY_OP_GT:
    case AST_BINARY_OP_GTE:
      return 1;
    default:
      return 0;
  }
}

int ast_binary_op_logical(int op) {
  switch (op) {
    case AST_BINARY_OP_LOGICAL_OR:
    case AST_BINARY_OP_LOGICAL_AND:
      return 1;
    default:
      return 0;
  }
}

const char *ast_expr_ident(struct ast_expr *expr) {
  switch (expr->type) {
    case AST_EXPR_TYPE_DEREF:
      return ast_expr_ident(expr->deref.target);
    case AST_EXPR_TYPE_VARIABLE:
      return expr->variable.ident.value.identv.ident;
    case AST_EXPR_TYPE_ARRAY_INDEX:
      return ast_expr_ident(expr->array_index.target);
    default:
      return NULL;
  }
}
