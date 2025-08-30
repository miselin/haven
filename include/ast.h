#ifndef _HAVEN_AST_H
#define _HAVEN_AST_H

#include <stddef.h>
#include <stdint.h>

#include "lex.h"
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
#define AST_DECL_TYPE_FOREIGN 6

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
#define AST_EXPR_TYPE_DEREF 7
#define AST_EXPR_TYPE_CALL 8
#define AST_EXPR_TYPE_VOID 9
#define AST_EXPR_TYPE_CAST 10
#define AST_EXPR_TYPE_UNARY 11
#define AST_EXPR_TYPE_IF 12
#define AST_EXPR_TYPE_ASSIGN 13
#define AST_EXPR_TYPE_REF 14
#define AST_EXPR_TYPE_LOAD 15
#define AST_EXPR_TYPE_ARRAY_INDEX 17
#define AST_EXPR_TYPE_MATCH 18
#define AST_EXPR_TYPE_NIL 20
#define AST_EXPR_TYPE_PATTERN_MATCH 21
#define AST_EXPR_TYPE_ENUM_INIT 22
#define AST_EXPR_TYPE_SIZEOF 24
#define AST_EXPR_TYPE_BOX 25
#define AST_EXPR_TYPE_UNBOX 26
#define AST_EXPR_TYPE_INITIALIZER 27

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
  struct ast_ty *ty;
  struct ast_ty parser_ty;

  uint64_t flags;

  struct ast_expr *init_expr;
};

struct ast_fdecl_param_metadata {
  char *name;
  uint64_t flags;
};

struct ast_fdecl {
  struct token ident;
  struct ast_ty *function_ty;
  struct ast_ty parsed_function_ty;

  uint64_t flags;

  struct ast_block *body;
  struct ast_fdecl_param_metadata *params;
  size_t num_params;

  int is_intrinsic;
  char intrinsic[256];
  struct ast_ty *intrinsic_tys;
  struct ast_ty *parsed_intrinsic_tys;
  size_t num_intrinsic_tys;
};

struct ast_tydecl {
  struct token ident;
  struct ast_ty parsed_ty;
  struct ast_ty *resolved;
};

struct ast_import {
  char path[256];
  struct ast_program *ast;
  enum ImportType type;
};

struct ast_foreign {
  char libname[256];
  struct ast_toplevel *decls;
};

struct ast_toplevel {
  int type;
  struct lex_locator loc;

  struct ast_toplevel *next;
  union toplevel_oneof {
    struct ast_vdecl vdecl;
    struct ast_fdecl fdecl;
    struct ast_tydecl tydecl;
    struct ast_import import;
    struct ast_foreign foreign;
  } toplevel;
};

struct ast_expr_constant {
  struct token constant;
  enum ast_ty_id type;
};

struct ast_block {
  struct ast_stmt *stmt;
  struct ast_stmt *last_stmt;
  struct lex_locator loc;
  struct ast_ty *ty;
};

struct ast_expr_binary {
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
  struct ast_ty *function_ty;
};

struct ast_expr_cast {
  struct ast_ty parsed_ty;
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

struct ast_expr_array_index {
  struct ast_expr *target;
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
  struct ast_template_ty *tmpls;
};

struct ast_expr_sizeof {
  struct ast_ty parsed_ty;  // only filled if expr is NULL
  struct ast_ty *resolved;
  struct ast_expr *expr;
};

struct ast_expr_box {
  struct ast_ty parsed_ty;  // only filled if expr is NULL
  struct ast_expr *expr;
};

union ast_expr_union {
  struct ast_expr_constant constant;
  struct ast_block block;
  struct ast_expr_binary binary;
  struct ast_expr_variable variable;
  struct ast_expr_list *list;
  struct ast_expr_deref deref;
  struct ast_expr_call call;
  struct ast_expr_cast cast;
  struct ast_expr_unary unary;
  struct ast_expr_if if_expr;
  struct ast_expr_assign assign;
  struct ast_expr_ref ref;
  struct ast_expr_load load;
  struct ast_expr_array_index array_index;
  struct ast_expr_match match;
  struct ast_expr_pattern_match pattern_match;
  struct ast_expr_enum_init enum_init;
  struct ast_expr_sizeof sizeof_expr;
  struct ast_expr_box box_expr;
};

struct ast_expr {
  int type;
  // The actual resolved type of the expression. Requires typecheck pass to be run.
  struct ast_ty *ty;
  // The parser will put whatever information it knows about the expression's type here.
  // The typecheck pass will use this to fill the actual type.
  struct ast_ty parsed_ty;
  struct lex_locator loc;

  union ast_expr_union expr;
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
  struct ast_ty *index_ty;
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
  } stmt;

  struct ast_stmt *next;
};

struct ast_visitor_node {
  struct ast_toplevel *toplevel;
  struct ast_block *block;
  struct ast_stmt *stmt;
  struct ast_expr *expr;
  size_t depth;
};

void free_ast(struct compiler *compiler, struct ast_program *ast);
void dump_ast(struct ast_program *ast);
void dump_expr(struct ast_expr *ast, int indent);

int emit_ast_as_code(struct ast_program *ast, FILE *stream);

void free_toplevel(struct compiler *compiler, struct ast_toplevel *ast);
void free_block(struct compiler *compiler, struct ast_block *ast, int heap);
void free_stmt(struct compiler *compiler, struct ast_stmt *ast);
void free_expr(struct compiler *compiler, struct ast_expr *ast);
void free_fdecl(struct compiler *compiler, struct ast_fdecl *ast, int heap);
void free_vdecl(struct compiler *compiler, struct ast_vdecl *ast, int heap);
void free_tydecl(struct compiler *compiler, struct ast_tydecl *ast, int heap);
void free_ty(struct compiler *compiler, struct ast_ty *ty, int heap);
void free_expr_list(struct compiler *compiler, struct ast_expr_list *list);

// Remove the given top-level node from the AST.
void ast_remove(struct compiler *compiler, struct ast_program *ast, struct ast_toplevel *node);

// Free a parser-generated type. These can have annoying heap pointers that aren't in the type
// repository and need to be freed.
void free_parser_ty(struct compiler *compiler, struct ast_ty *ty);

const char *ast_binary_op_to_str(int op);
const char *ast_unary_op_to_str(int op);
const char *ast_logical_op_to_str(int op);

int ast_binary_op_conditional(int op);
int ast_binary_op_logical(int op);

const char *ast_expr_ident(struct ast_expr *expr);

enum VisitorResult {
  // Continue in the normal AST visit order
  VisitorContinue,
  // Don't process any children of this node, continue to the next sibling
  VisitorSkipChildren,
  // Stop visiting nodes altogether
  VisitorStop,
};

typedef enum VisitorResult (*ast_visitor_fn)(struct ast_visitor_node *node, void *user_data);

void ast_visit(struct compiler *compiler, struct ast_program *ast, ast_visitor_fn visit,
               void *user_data);

int ast_serialize(struct ast_program *ast, char *buffer, size_t *len);
int ast_deserialize(struct compiler *compiler, char *buffer, size_t len, struct ast_program **ast);

#endif
