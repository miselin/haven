#ifndef _MATTC_AST_H
#define _MATTC_AST_H

#include <stddef.h>
#include <stdint.h>

#include "lex.h"
#include "tokens.h"
#include "types.h"

#define DECL_FLAG_PUB (1 << 0)
#define DECL_FLAG_MUT (1 << 1)
#define DECL_FLAG_VARARG (1 << 2)
#define DECL_FLAG_TEMPORARY (1 << 3)

#define AST_STMT_TYPE_EXPR 1
#define AST_STMT_TYPE_LET 2
#define AST_STMT_TYPE_ITER 3
#define AST_STMT_TYPE_STORE 4
#define AST_STMT_TYPE_RETURN 5

#define AST_EXPR_TYPE_CONSTANT 1
#define AST_EXPR_TYPE_BLOCK 2
#define AST_EXPR_TYPE_BINARY 3
#define AST_EXPR_TYPE_VARIABLE 4
#define AST_EXPR_TYPE_LOGICAL 5
#define AST_EXPR_TYPE_LIST 6
#define AST_EXPR_TYPE_DEREF 7
#define AST_EXPR_TYPE_CALL 8
#define AST_EXPR_TYPE_VOID 9
#define AST_EXPR_TYPE_CAST 10
#define AST_EXPR_TYPE_UNARY 11
#define AST_EXPR_TYPE_IF 12
#define AST_EXPR_TYPE_ASSIGN 13
#define AST_EXPR_TYPE_REF 14
#define AST_EXPR_TYPE_LOAD 15
#define AST_EXPR_TYPE_BOOLEAN 16
#define AST_EXPR_TYPE_ARRAY_INDEX 17

#define AST_BINARY_OP_ADD 1
#define AST_BINARY_OP_SUB 2
#define AST_BINARY_OP_MUL 3
#define AST_BINARY_OP_DIV 4
#define AST_BINARY_OP_MOD 5
#define AST_BINARY_OP_BITOR 6
#define AST_BINARY_OP_BITAND 7
#define AST_BINARY_OP_BITXOR 8
#define AST_BINARY_OP_LSHIFT 9
#define AST_BINARY_OP_RSHIFT 10

#define AST_UNARY_OP_NEG 1
#define AST_UNARY_OP_NOT 2
#define AST_UNARY_OP_COMP 3

#define AST_LOGICAL_OP_OR 1
#define AST_LOGICAL_OP_AND 2

struct ast_program {
  struct ast_toplevel *decls;
};

struct ast_vdecl {
  struct token ident;
  struct ast_ty ty;

  int flags;

  struct ast_expr *init_expr;
};

struct ast_fdecl {
  struct token ident;
  struct ast_ty retty;

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
  int field;
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

struct ast_expr_cast {
  struct ast_ty ty;
  struct ast_expr *expr;
};

struct ast_expr_unary {
  int op;
  struct ast_expr *expr;
};

struct ast_expr_if {
  struct ast_expr *cond;
  int has_else;
  struct ast_block then_block;
  struct ast_block else_block;
};

struct ast_expr_assign {
  struct token ident;
  struct ast_expr *expr;
};

struct ast_expr_ref {
  struct ast_expr *expr;
};

struct ast_expr_load {
  struct ast_expr *expr;
};

struct ast_expr_boolean {
  int op;
  struct ast_expr *lhs;
  struct ast_expr *rhs;
};

struct ast_expr_array_index {
  struct token ident;
  struct ast_expr *index;
};

struct ast_expr {
  int type;
  struct ast_ty ty;
  struct lex_locator loc;
  union {
    struct ast_expr_constant constant;
    struct ast_block block;
    struct ast_expr_binary binary;
    struct ast_expr_variable variable;
    struct ast_expr_logical logical;
    struct ast_expr_list *list;
    struct ast_expr_deref deref;
    struct ast_expr_call call;
    struct ast_expr_cast cast;
    struct ast_expr_unary unary;
    struct ast_expr_if if_expr;
    struct ast_expr_assign assign;
    struct ast_expr_ref ref;
    struct ast_expr_load load;
    struct ast_expr_boolean boolean;
    struct ast_expr_array_index array_index;
  };
};

struct ast_range {
  struct ast_expr *start;
  struct ast_expr *end;
  struct ast_expr *step;  // if null, step is +1
};

struct ast_stmt_iter {
  struct ast_range range;
  struct ast_expr_variable index;
  struct ast_block block;

  // only set after typecheck pass
  struct ast_vdecl *index_vdecl;
};

struct ast_stmt_store {
  struct ast_expr *lhs;
  struct ast_expr *rhs;
};

struct ast_stmt {
  int type;

  union {
    struct ast_expr *expr;
    struct ast_vdecl let;
    struct ast_stmt_iter iter;
    struct ast_stmt_store store;
  };

  struct ast_stmt *next;
};

void free_ast(struct ast_program *ast);
void dump_ast(struct ast_program *ast);

void free_toplevel(struct ast_toplevel *ast);
void free_block(struct ast_block *ast, int heap);
void free_stmt(struct ast_stmt *ast);
void free_expr(struct ast_expr *ast);
void free_fdecl(struct ast_fdecl *ast, int heap);
void free_vdecl(struct ast_vdecl *ast, int heap);
void free_ty(struct ast_ty *ty, int heap);
void free_expr_list(struct ast_expr_list *list);

const char *ast_binary_op_to_str(int op);
const char *ast_unary_op_to_str(int op);
const char *ast_logical_op_to_str(int op);

#endif
