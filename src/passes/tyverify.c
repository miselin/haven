/**
 * Second typecheck pass - just verifies that the AST contains no TBD or ERROR types.
 */

#include <stdarg.h>
#include <stdlib.h>

#include "ast.h"
#include "types.h"

struct tyverify_state {
  struct ast_program *ast;
  struct compiler *compiler;
  int errors;
};

static int typecheck_verify_toplevel(struct tyverify_state *state, struct ast_toplevel *ast);
static int typecheck_verify_expr(struct tyverify_state *state, struct ast_expr *ast);
static int typecheck_verify_struct_decl(struct tyverify_state *state, struct ast_ty *decl);

static enum VisitorResult tyverify_visitor(struct ast_visitor_node *node, void *user_data);

static int is_bad_type(struct ast_ty *ty) {
  // custom types should have been resolved by now
  return type_is_error(ty) || type_is_tbd(ty) || ty->ty == AST_TYPE_CUSTOM;
}

int typecheck_verify_ast(struct compiler *compiler, struct ast_program *ast) {
  struct tyverify_state state = {
      .ast = ast,
      .compiler = compiler,
      .errors = 0,
  };

  ast_visit(compiler, ast, tyverify_visitor, &state);
  return state.errors;
}

static enum VisitorResult tyverify_visitor(struct ast_visitor_node *node, void *user_data) {
  struct tyverify_state *state = user_data;

  if (node->toplevel) {
    typecheck_verify_toplevel(state, node->toplevel);
  } else if (node->expr) {
    typecheck_verify_expr(state, node->expr);
  }

  return state->errors ? VisitorStop : VisitorContinue;
}

static int typecheck_verify_toplevel(struct tyverify_state *state, struct ast_toplevel *ast) {
  if (ast->type == AST_DECL_TYPE_FDECL) {
    if (is_bad_type(ast->fdecl.function_ty->function.retty)) {
      compiler_log(state->compiler, LogLevelError, "tyverify",
                   "function %s has unresolved return type", ast->fdecl.ident.value.identv.ident);
      ++state->errors;
      return -1;
    }

    for (size_t i = 0; i < ast->fdecl.num_params; i++) {
      struct ast_ty *param_ty = ast->fdecl.function_ty->function.param_types[i];
      if (is_bad_type(param_ty)) {
        compiler_log(state->compiler, LogLevelError, "tyverify",
                     "function %s has unresolved parameter type for parameter %zd",
                     ast->fdecl.ident.value.identv.ident, i + 1);
        ++state->errors;
        return -1;
      }
    }
  } else if (ast->type == AST_DECL_TYPE_VDECL) {
    if (is_bad_type(ast->vdecl.ty)) {
      compiler_log(state->compiler, LogLevelError, "tyverify", "variable %s has unresolved type",
                   ast->vdecl.ident.value.identv.ident);
      ++state->errors;
      return -1;
    }
  } else if (ast->type == AST_DECL_TYPE_TYDECL) {
    if (ast->tydecl.resolved->ty == AST_TYPE_STRUCT) {
      return typecheck_verify_struct_decl(state, ast->tydecl.resolved);
    }
  }

  return 0;
}

static int typecheck_verify_expr(struct tyverify_state *state, struct ast_expr *ast) {
  if (is_bad_type(ast->ty)) {
    compiler_log(state->compiler, LogLevelError, "tyverify", "expression node has unresolved type");
    ++state->errors;
  }

  return 0;
}

static int typecheck_verify_struct_decl(struct tyverify_state *state, struct ast_ty *decl) {
  struct ast_struct_field *field = decl->structty.fields;
  while (field) {
    if (is_bad_type(field->ty)) {
      compiler_log(state->compiler, LogLevelError, "tyverify",
                   "struct %s field %s has unresolved type", decl->name, field->name);
      ++state->errors;
      return -1;
    }

    field = field->next;
  }

  return 0;
}
