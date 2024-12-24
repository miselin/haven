#ifndef _HAVEN_AST_H
#define _HAVEN_AST_H

#include <stddef.h>
#include <stdint.h>

#include "lex.h"
#include "tokens.h"
#include "types.h"

#define DECL_FLAG_PUB (1U << 0)
#define DECL_FLAG_MUT (1U << 1)
#define DECL_FLAG_VARARG (1U << 2)
#define DECL_FLAG_TEMPORARY (1U << 3)
#define DECL_FLAG_IMPURE (1U << 4)
#define DECL_FLAG_EXTERN (1U << 5)

#define AST_DECL_TYPE_VDECL 1
#define AST_DECL_TYPE_FDECL 2
#define AST_DECL_TYPE_TYDECL 3
#define AST_DECL_TYPE_PREPROC 4  // tracked in AST but not used after parse
#define AST_DECL_TYPE_IMPORT 5   // fake AST node, import merges ASTs

#define AST_STMT_TYPE_EXPR 1
#define AST_STMT_TYPE_LET 2
#define AST_STMT_TYPE_ITER 3
#define AST_STMT_TYPE_STORE 4
#define AST_STMT_TYPE_RETURN 5
#define AST_STMT_TYPE_DEFER 6
#define AST_STMT_TYPE_WHILE 7
#define AST_STMT_TYPE_BREAK 8
#define AST_STMT_TYPE_CONTINUE 9

#define AST_EXPR_TYPE_CONSTANT 1
#define AST_EXPR_TYPE_BLOCK 2
#define AST_EXPR_TYPE_BINARY 3
#define AST_EXPR_TYPE_VARIABLE 4
#define AST_EXPR_TYPE_LOGICAL 5  // should be removed
#define AST_EXPR_TYPE_LIST 6     // should be removed
#define AST_EXPR_TYPE_DEREF 7
#define AST_EXPR_TYPE_CALL 8
#define AST_EXPR_TYPE_VOID 9
#define AST_EXPR_TYPE_CAST 10
#define AST_EXPR_TYPE_UNARY 11
#define AST_EXPR_TYPE_IF 12
#define AST_EXPR_TYPE_ASSIGN 13
#define AST_EXPR_TYPE_REF 14
#define AST_EXPR_TYPE_LOAD 15
#define AST_EXPR_TYPE_BOOLEAN 16  // should be removed
#define AST_EXPR_TYPE_ARRAY_INDEX 17
#define AST_EXPR_TYPE_MATCH 18
#define AST_EXPR_TYPE_STRUCT_INIT 19
#define AST_EXPR_TYPE_NIL 20
#define AST_EXPR_TYPE_PATTERN_MATCH 21
#define AST_EXPR_TYPE_ENUM_INIT 22
#define AST_EXPR_TYPE_UNION_INIT 23
#define AST_EXPR_TYPE_SIZEOF 24

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
#define AST_BINARY_OP_LOGICAL_OR 11
#define AST_BINARY_OP_LOGICAL_AND 12
#define AST_BINARY_OP_EQUAL 13
#define AST_BINARY_OP_NOT_EQUAL 14
#define AST_BINARY_OP_LT 15
#define AST_BINARY_OP_LTE 16
#define AST_BINARY_OP_GT 17
#define AST_BINARY_OP_GTE 18

#define AST_UNARY_OP_NEG 1
#define AST_UNARY_OP_NOT 2
#define AST_UNARY_OP_COMP 3

#define AST_LOGICAL_OP_OR 1
#define AST_LOGICAL_OP_AND 2

struct ast_program {
  struct lex_locator loc;
  struct ast_toplevel *decls;
};

struct ast_vdecl {
  struct token ident;
  struct ast_ty ty;

  uint64_t flags;

  struct ast_expr *init_expr;
};

struct ast_fdecl {
  struct token ident;
  struct ast_ty retty;

  uint64_t flags;

  struct ast_block *body;
  struct ast_vdecl **params;
  size_t num_params;
};

struct ast_tydecl {
  struct token ident;
  struct ast_ty ty;
};

struct ast_toplevel {
  int type;
  struct lex_locator loc;

  struct ast_toplevel *next;
  union {
    struct ast_vdecl vdecl;
    struct ast_fdecl fdecl;
    struct ast_tydecl tydecl;
  };
};

struct ast_expr_constant {
  struct token constant;
};

struct ast_block {
  struct ast_stmt *stmt;
  struct lex_locator loc;
  struct ast_ty ty;
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
  int is_ptr;
  struct ast_expr *target;
  struct token field;  // typecheck resolves this and fills field_idx
  size_t field_idx;
};

struct ast_expr_list {
  struct ast_expr *expr;
  struct ast_expr_list *next;
  size_t num_elements;
};

struct ast_expr_call {
  struct token ident;
  struct ast_fdecl *fdecl;
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

struct ast_expr_elseif {
  struct ast_expr *cond;
  struct ast_block block;
  struct ast_expr_elseif *next;
};

struct ast_expr_if {
  struct ast_expr *cond;
  struct ast_expr_elseif *elseifs;
  int has_else;
  struct ast_block then_block;
  struct ast_block else_block;
};

struct ast_expr_assign {
  struct ast_expr *lhs;
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

struct ast_expr_match {
  struct ast_expr *expr;
  struct ast_expr_match_arm *arms;
  struct ast_expr_match_arm *otherwise;
  size_t num_arms;
};

struct ast_expr_match_arm {
  struct ast_expr *pattern;
  struct ast_expr *expr;
  struct ast_expr_match_arm *next;
};

struct ast_expr_pattern_match {
  // name of enum type
  struct token enum_name;
  // name of enum field to match
  struct token name;
  // inner variable to bind, if any
  struct ast_vdecl *inner_vdecl;
  // bindings are to be ignored
  int bindings_ignored;
};

struct ast_expr_enum_init {
  struct token enum_ty_name;
  struct token enum_val_name;
  struct ast_expr *inner;
};

struct ast_expr_union_init {
  struct ast_ty ty;
  struct token field;
  struct ast_expr *inner;
};

struct ast_expr_sizeof {
  struct ast_ty ty;
  struct ast_expr *expr;
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
    struct ast_expr_match match;
    struct ast_expr_pattern_match pattern_match;
    struct ast_expr_enum_init enum_init;
    struct ast_expr_union_init union_init;
    struct ast_expr_sizeof sizeof_expr;
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

struct ast_stmt_while {
  struct ast_expr *cond;
  struct ast_block block;
};

struct ast_stmt {
  int type;
  struct lex_locator loc;

  union {
    struct ast_expr *expr;
    struct ast_vdecl let;
    struct ast_stmt_iter iter;
    struct ast_stmt_store store;
    struct ast_stmt_while while_stmt;
  };

  struct ast_stmt *next;
};

void free_ast(struct ast_program *ast);
void dump_ast(struct ast_program *ast);
void dump_expr(struct ast_expr *ast, int indent);

int emit_ast_as_code(struct ast_program *ast, FILE *stream);

void free_toplevel(struct ast_toplevel *ast);
void free_block(struct ast_block *ast, int heap);
void free_stmt(struct ast_stmt *ast);
void free_expr(struct ast_expr *ast);
void free_fdecl(struct ast_fdecl *ast, int heap);
void free_vdecl(struct ast_vdecl *ast, int heap);
void free_tydecl(struct ast_tydecl *ast, int heap);
void free_ty(struct ast_ty *ty, int heap);
void free_expr_list(struct ast_expr_list *list);

const char *ast_binary_op_to_str(int op);
const char *ast_unary_op_to_str(int op);
const char *ast_logical_op_to_str(int op);

int ast_binary_op_conditional(int op);
int ast_binary_op_logical(int op);

const char *ast_expr_ident(struct ast_expr *expr);

#endif
