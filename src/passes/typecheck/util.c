#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "internal.h"
#include "types.h"

int binary_mismatch_ok(int op, struct ast_ty *lhs, struct ast_ty *rhs) {
  // float/fvec can mix but only for certain operations
  // e.g. scale, division
  if ((lhs->ty == AST_TYPE_FVEC && rhs->ty == AST_TYPE_FLOAT) ||
      (lhs->ty == AST_TYPE_FLOAT && rhs->ty == AST_TYPE_FVEC)) {
    return op == AST_BINARY_OP_MUL || op == AST_BINARY_OP_DIV || op == AST_BINARY_OP_MOD;
  }

  // pointer arithmetic
  if (lhs->ty == AST_TYPE_POINTER || rhs->ty == AST_TYPE_POINTER) {
    return op == AST_BINARY_OP_ADD || op == AST_BINARY_OP_SUB;
  }

  // matrix math
  if (lhs->ty == AST_TYPE_MATRIX || rhs->ty == AST_TYPE_MATRIX) {
    return check_matrix_binary_op(op, lhs, rhs);
  }

  return 0;
}

int check_matrix_binary_op(int op, struct ast_ty *lhs, struct ast_ty *rhs) {
  if (lhs->ty != AST_TYPE_MATRIX && rhs->ty != AST_TYPE_MATRIX) {
    return 0;
  }

  // matrix vs matrix ops
  if (lhs->ty == AST_TYPE_MATRIX && rhs->ty == AST_TYPE_MATRIX) {
    if (op == AST_BINARY_OP_MUL) {
      // # of columns in left must match # of rows in right for multiplication
      return lhs->oneof.matrix.cols == rhs->oneof.matrix.rows;
    }

    // other ops require the same dimensions
    if (lhs->oneof.matrix.cols != rhs->oneof.matrix.cols ||
        lhs->oneof.matrix.rows != rhs->oneof.matrix.rows) {
      return 0;
    }

    // only addition and subtraction are valid for matrices
    return op == AST_BINARY_OP_ADD || op == AST_BINARY_OP_SUB;
  }

  if (lhs->ty == AST_TYPE_MATRIX && rhs->ty == AST_TYPE_FVEC) {
    // fvec must be on the LHS for matrix multiplication
    return 0;
  }

  if (lhs->ty == AST_TYPE_FVEC && rhs->ty == AST_TYPE_MATRIX) {
    if (op != AST_BINARY_OP_MUL) {
      return 0;
    }

    // # of rows must match the width of the vector
    return lhs->oneof.matrix.cols == rhs->oneof.fvec.width;
  }

  if (lhs->ty != AST_TYPE_MATRIX || rhs->ty != AST_TYPE_MATRIX) {
    // make LHS the matrix type for comparisons
    if (lhs->ty != AST_TYPE_MATRIX) {
      struct ast_ty tmp = *lhs;
      *lhs = *rhs;
      *rhs = tmp;
    }
  }

  if (lhs->ty == AST_TYPE_MATRIX && rhs->ty == AST_TYPE_FLOAT) {
    // matrix scalar multiply is valid
    return op == AST_BINARY_OP_MUL;
  }

  return 0;
}

int deref_to_index(const char *deref) {
  if (isdigit(*deref)) {
    // numeric deref
    // TODO: check that endptr is the end of deref; fully consume the string
    return (int)strtol(deref, NULL, 10);
  }

  if (deref[1] == 0) {
    // single character deref
    switch (deref[0]) {
      // XYZW
      case 'x':
      case 'y':
      case 'z':
        return deref[0] - 'x';
      case 'w':
        return 3;

      // RGBA
      case 'r':
        return 0;
      case 'g':
        return 1;
      case 'b':
        return 2;
      case 'a':
        return 3;
    }
  }

  return -1;
}
