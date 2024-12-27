#include <stdio.h>

#include "ast.h"
#include "internal.h"

int binary_op(enum token_id token) {
  switch (token) {
    case TOKEN_PLUS:
      return AST_BINARY_OP_ADD;
    case TOKEN_MINUS:
      return AST_BINARY_OP_SUB;
    case TOKEN_ASTERISK:
      return AST_BINARY_OP_MUL;
    case TOKEN_FSLASH:
      return AST_BINARY_OP_DIV;
    case TOKEN_PERCENT:
      return AST_BINARY_OP_MOD;
    case TOKEN_BITOR:
      return AST_BINARY_OP_BITOR;
    case TOKEN_BITAND:
      return AST_BINARY_OP_BITAND;
    case TOKEN_BITXOR:
      return AST_BINARY_OP_BITXOR;
    case TOKEN_LSHIFT:
      return AST_BINARY_OP_LSHIFT;
    case TOKEN_RSHIFT:
      return AST_BINARY_OP_RSHIFT;
    case TOKEN_EQUALS:
      return AST_BINARY_OP_EQUAL;
    case TOKEN_NE:
      return AST_BINARY_OP_NOT_EQUAL;
    case TOKEN_LT:
      return AST_BINARY_OP_LT;
    case TOKEN_LTE:
      return AST_BINARY_OP_LTE;
    case TOKEN_GT:
      return AST_BINARY_OP_GT;
    case TOKEN_GTE:
      return AST_BINARY_OP_GTE;
    case TOKEN_OR:
      return AST_BINARY_OP_LOGICAL_OR;
    case TOKEN_AND:
      return AST_BINARY_OP_LOGICAL_AND;
    default:
      return -1;
  }
}

int binary_op_prec(int op) {
  switch (op) {
    case AST_BINARY_OP_LOGICAL_OR:
      return 5;
    case AST_BINARY_OP_LOGICAL_AND:
      return 10;
    case AST_BINARY_OP_BITOR:
      return 15;
    case AST_BINARY_OP_BITXOR:
      return 20;
    case AST_BINARY_OP_BITAND:
      return 25;
    case AST_BINARY_OP_EQUAL:
    case AST_BINARY_OP_NOT_EQUAL:
      return 30;
    case AST_BINARY_OP_LT:
    case AST_BINARY_OP_LTE:
    case AST_BINARY_OP_GT:
    case AST_BINARY_OP_GTE:
      return 35;
    case AST_BINARY_OP_LSHIFT:
    case AST_BINARY_OP_RSHIFT:
      return 40;
    case AST_BINARY_OP_ADD:
    case AST_BINARY_OP_SUB:
      return 45;
    case AST_BINARY_OP_MUL:
    case AST_BINARY_OP_DIV:
    case AST_BINARY_OP_MOD:
      return 50;
    default:
      fprintf(stderr, "returning lowest possible precedence for unknown binary op %d \n", op);
  }

  return 0;
}
