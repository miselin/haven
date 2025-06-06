#ifndef _HAVEN_PASSES_TYPECHECK_INTERNAL_H
#define _HAVEN_PASSES_TYPECHECK_INTERNAL_H

#include "ast.h"
#include "kv.h"
#include "types.h"

struct scope_entry {
  struct ast_fdecl *fdecl;
  struct ast_ty *ty;
  uint64_t decl_flags;
};

struct alias_entry {
  struct ast_ty *ty;
};

struct typecheck {
  struct ast_program *ast;

  struct scope *scope;

  int errors;

  struct kv *aliases;

  struct ast_ty void_type;
  struct ast_ty error_type;
  struct ast_ty tbd_type;

  struct compiler *compiler;

  struct type_repository *type_repo;
};

#ifdef __cplusplus
extern "C" {
#endif

struct ast_ty *typecheck_expr(struct typecheck *typecheck, struct ast_expr *ast)
    __attribute__((warn_unused_result));
// Allow TBD types to be generated by the expression
struct ast_ty *typecheck_expr_with_tbds(struct typecheck *typecheck, struct ast_expr *ast);
struct ast_ty *typecheck_expr_inner(struct typecheck *typecheck, struct ast_expr *ast);

// Resolve the given type as needed. Assumes that the type is from the type repository.
struct ast_ty *resolve_type(struct typecheck *typecheck, struct ast_ty *ty);

// Resolve the given type as needed. Assumes that the type is from the parser and may need to be
// copied or otherwise modified to be resolved.
struct ast_ty *resolve_parsed_type(struct typecheck *typecheck, struct ast_ty *ty);

// Patch TBDs caused by recursive definitions in the given type. Requires the actual type to
// have been defined or else this will retain the TBDs.
void patch_type_tbds(struct typecheck *typecheck, struct ast_ty *ty, struct ast_ty *parser_ty);

void resolve_template_type(struct typecheck *typecheck, struct ast_template_ty *templates,
                           struct ast_ty *ty);

// If allowed, makes from and to the same type using implicit conversion rules
// Does nothing if implicit conversion is disallowed or irrelevant.
int maybe_implicitly_convert(struct ast_ty **from, struct ast_ty **to);

int deref_to_index(const char *deref);

int binary_mismatch_ok(int op, struct ast_ty *lhs, struct ast_ty *rhs);
int check_matrix_binary_op(int op, struct ast_ty *lhs, struct ast_ty *rhs);

struct ast_ty *typecheck_block(struct typecheck *typecheck, struct ast_block *ast);
struct ast_ty *typecheck_pattern_match(struct typecheck *typecheck, struct ast_expr *ast,
                                       struct ast_expr_pattern_match *pattern,
                                       struct ast_ty *match_ty);

int typecheck_verify_ast(struct compiler *compiler, struct ast_program *ast);

__attribute__((format(printf, 3, 4))) void typecheck_diag_expr(struct typecheck *typecheck,
                                                               struct ast_expr *expr,
                                                               const char *msg, ...);

#ifdef __cplusplus
}
#endif

#endif
