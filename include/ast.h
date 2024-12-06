#ifndef _MATTC_AST_H
#define _MATTC_AST_H

#include <stddef.h>
#include <stdint.h>

#include "lex.h"
#include "tokens.h"

#define DECL_FLAG_PUB (1 << 0)
#define DECL_FLAG_MUT (1 << 1)
#define DECL_FLAG_VARARG (1 << 2)
#define DECL_FLAG_TEMPORARY (1 << 3)

#define AST_STMT_TYPE_EXPR 1
#define AST_STMT_TYPE_LET 2

#define AST_EXPR_TYPE_CONSTANT 1
#define AST_EXPR_TYPE_BLOCK 2
#define AST_EXPR_TYPE_BINARY 3
#define AST_EXPR_TYPE_VARIABLE 4
#define AST_EXPR_TYPE_LOGICAL 5
#define AST_EXPR_TYPE_LIST 6
#define AST_EXPR_TYPE_DEREF 7
#define AST_EXPR_TYPE_CALL 8
#define AST_EXPR_TYPE_VOID 9

#define AST_BINARY_OP_ADD 1
#define AST_BINARY_OP_SUB 2
#define AST_BINARY_OP_MUL 3
#define AST_BINARY_OP_DIV 4
#define AST_BINARY_OP_MOD 5
#define AST_BINARY_OP_BITOR 6
#define AST_BINARY_OP_BITAND 7
#define AST_BINARY_OP_BITXOR 8

#define AST_LOGICAL_OP_OR 1
#define AST_LOGICAL_OP_AND 2

enum ast_ty {
  AST_TYPE_ERROR = 0,
  AST_TYPE_TBD,  // yet to be determined by type checking pass
  AST_TYPE_INTEGER,
  AST_TYPE_STRING,
  AST_TYPE_CHAR,
  AST_TYPE_FLOAT,
  AST_TYPE_FVEC,
  AST_TYPE_VOID,
};

struct ast_program {
  struct ast_toplevel *decls;
};

struct ast_vdecl {
  struct token ident;
  enum ast_ty ty;

  int flags;

  struct ast_expr *init_expr;
};

struct ast_fdecl {
  struct token ident;
  enum ast_ty retty;

  int flags;

  struct ast_block *body;
  struct ast_vdecl **params;
  size_t num_params;
};

struct ast_toplevel {
  int is_fn;

  struct ast_toplevel *next;
  union {
    struct ast_vdecl vdecl;
    struct ast_fdecl fdecl;
  };
};

struct ast_expr_constant {
  struct token constant;
};

struct ast_block {
  struct ast_stmt *stmt;
};

struct ast_expr_binary {
  int op;
  struct ast_expr *lhs;
  struct ast_expr *rhs;
};

// Same as binary, but short-circuits evaluation based on the operator
struct ast_expr_logical {
  int op;
  struct ast_expr *lhs;
  struct ast_expr *rhs;
};

struct ast_expr_variable {
  struct token ident;
};

struct ast_expr_deref {
  struct token ident;
  struct token field;
};

struct ast_expr_list {
  struct ast_expr *expr;
  struct ast_expr_list *next;
  size_t num_elements;
};

struct ast_expr_call {
  struct token ident;
  struct ast_expr_list *args;
};

struct ast_expr {
  int type;
  enum ast_ty ty;
  union {
    struct ast_expr_constant constant;
    struct ast_block block;
    struct ast_expr_binary binary;
    struct ast_expr_variable variable;
    struct ast_expr_logical logical;
    struct ast_expr_list *list;
    struct ast_expr_deref deref;
    struct ast_expr_call call;
  };
};

struct ast_stmt {
  int type;

  union {
    struct ast_expr *expr;
    struct ast_vdecl let;
  };

  struct ast_stmt *next;
};

#endif
