/** Routines to emit an AST back as source code. */

#include <inttypes.h>
#include <stdio.h>

#include "ast.h"
#include "tokens.h"
#include "types.h"

static int code_emit_toplevel(FILE *stream, struct ast_toplevel *ast);
static int code_emit_block(FILE *stream, struct ast_block *ast, int indent);
static int code_emit_stmt(FILE *stream, struct ast_stmt *ast, int indent);
static int code_emit_fdecl(FILE *stream, struct ast_fdecl *ast, int indent);
static int code_emit_vdecl(FILE *stream, struct ast_vdecl *ast, int indent);
static int code_emit_tydecl(FILE *stream, struct ast_tydecl *ast, int indent);
static int code_emit_expr(FILE *stream, struct ast_expr *ast, int indent);
static int code_emit_ty(FILE *stream, struct ast_ty *ast);

int emit_ast_as_code(struct ast_program *ast, FILE *stream) {
  struct ast_toplevel *decl = ast->decls;
  while (decl) {
    if (code_emit_toplevel(stream, decl) < 0) {
      return -1;
    }
    decl = decl->next;
  }
  return 0;
}

#define INDENTED(stream, level, ...) \
  do {                               \
    print_indent(stream, level);     \
    fprintf(stream, __VA_ARGS__);    \
  } while (0)

static void print_indent(FILE *stream, int level) {
  for (int i = 0; i < level; i++) {
    fprintf(stream, "  ");
  }
}

static int code_emit_toplevel(FILE *stream, struct ast_toplevel *ast) {
  if (ast->type == AST_DECL_TYPE_FDECL) {
    return code_emit_fdecl(stream, &ast->fdecl, 0);
  } else if (ast->type == AST_DECL_TYPE_VDECL) {
    return code_emit_vdecl(stream, &ast->vdecl, 0);
  } else if (ast->type == AST_DECL_TYPE_TYDECL) {
    return code_emit_tydecl(stream, &ast->tydecl, 0);
  } else if (ast->type == AST_DECL_TYPE_PREPROC) {
    // no-op
  } else {
    fprintf(stderr, "unhandled toplevel type in source code emit %d\n", ast->type);
    return -1;
  }

  return 0;
}

static int code_emit_block(FILE *stream, struct ast_block *ast, int indent) {
  INDENTED(stream, indent, "{\n");
  struct ast_stmt *stmt = ast->stmt;
  while (stmt) {
    if (code_emit_stmt(stream, stmt, indent + 1) < 0) {
      return -1;
    }
    if (stmt->next) {
      fprintf(stream, ";\n");
    } else {
      fprintf(stream, "\n");
    }
    stmt = stmt->next;
  }
  INDENTED(stream, indent, "}\n");
  return 0;
}

static int code_emit_stmt(FILE *stream, struct ast_stmt *ast, int indent) {
  switch (ast->type) {
    case AST_STMT_TYPE_EXPR:
      print_indent(stream, indent);
      code_emit_expr(stream, ast->expr, indent);
      break;

    case AST_STMT_TYPE_LET:
      INDENTED(stream, indent, "let ");
      code_emit_vdecl(stream, &ast->let, 0);
      fprintf(stream, " = ");
      code_emit_expr(stream, ast->let.init_expr, indent);
      break;

    case AST_STMT_TYPE_ITER:
      INDENTED(stream, indent, "iter ");
      code_emit_expr(stream, ast->iter.range.start, indent);
      fprintf(stream, ":");
      code_emit_expr(stream, ast->iter.range.end, indent);
      if (ast->iter.range.step) {
        fprintf(stream, ":");
        code_emit_expr(stream, ast->iter.range.step, indent);
      }
      fprintf(stream, " ");
      // TODO: emit index variable
      code_emit_block(stream, &ast->iter.block, indent);
      break;

    case AST_STMT_TYPE_STORE:
      code_emit_expr(stream, ast->store.lhs, indent);
      fprintf(stream, " = ");
      code_emit_expr(stream, ast->store.rhs, indent);
      break;

    case AST_STMT_TYPE_DEFER:
      fprintf(stream, "defer ");
      code_emit_expr(stream, ast->expr, indent);
      break;

    default:
      fprintf(stderr, "unhandled statement type %d\n", ast->type);
      return -1;
  }

  return 0;
}

static int code_emit_fdecl(FILE *stream, struct ast_fdecl *ast, int indent) {
  fprintf(stream, "\n");

  if (ast->flags & DECL_FLAG_PUB) {
    fprintf(stream, "pub ");
  }
  if (ast->flags & DECL_FLAG_IMPURE) {
    fprintf(stream, "impure ");
  }
  code_emit_ty(stream, &ast->parsed_retty);
  fprintf(stream, " fn %s(", ast->ident.value.identv.ident);
  for (size_t i = 0; i < ast->num_params; i++) {
    if (i > 0) {
      fprintf(stream, ", ");
    }
    code_emit_vdecl(stream, ast->params[i], indent);
  }
  fprintf(stream, ")");
  if (ast->body) {
    fprintf(stream, " ");
    code_emit_block(stream, ast->body, indent);
  } else {
    fprintf(stream, ";\n");
  }
  return 0;
}

static int code_emit_vdecl(FILE *stream, struct ast_vdecl *ast, int indent) {
  print_indent(stream, indent);
  if (!type_is_tbd(ast->ty)) {
    code_emit_ty(stream, ast->ty);
    fprintf(stream, " ");
  }
  fprintf(stream, "%s", ast->ident.value.identv.ident);
  return 0;
}

static int code_emit_tydecl(FILE *stream, struct ast_tydecl *ast, int indent) {
  INDENTED(stream, indent, "type %s = ", ast->ident.value.identv.ident);
  code_emit_ty(stream, &ast->parsed_ty);
  fprintf(stream, "\n");
  return 0;
}

static int code_emit_expr(FILE *stream, struct ast_expr *ast, int indent) {
  switch (ast->type) {
    case AST_EXPR_TYPE_CONSTANT: {
      switch (ast->constant.constant.ident) {
        case TOKEN_INTEGER:
          fprintf(stream, "%" PRIi64, ast->constant.constant.value.intv.val);
          break;

        case TOKEN_FLOAT:
          fprintf(stream, "%s", ast->constant.constant.value.floatv.buf);
          break;

        default:
          fprintf(stderr, "<unimpl-constant-%d>", ast->constant.constant.ident);
          break;
      }
    } break;

    case AST_EXPR_TYPE_BINARY:
      code_emit_expr(stream, ast->binary.lhs, indent);
      fprintf(stream, " %s ", ast_binary_op_to_str(ast->binary.op));
      code_emit_expr(stream, ast->binary.rhs, indent);
      break;

    case AST_EXPR_TYPE_VARIABLE:
      fprintf(stream, "%s", ast->variable.ident.value.identv.ident);
      break;

    case AST_EXPR_TYPE_CALL: {
      fprintf(stream, "%s(", ast->call.ident.value.identv.ident);
      struct ast_expr_list *node = ast->call.args;
      while (node) {
        code_emit_expr(stream, node->expr, indent);
        if (node->next) {
          fprintf(stream, ", ");
        }
        node = node->next;
      }
      fprintf(stream, ")");
    } break;

    case AST_EXPR_TYPE_BLOCK:
      code_emit_block(stream, &ast->block, 0);
      break;

    case AST_EXPR_TYPE_STRUCT_INIT: {
      fprintf(stream, "struct {");
      struct ast_expr_list *node = ast->list;
      while (node) {
        code_emit_expr(stream, node->expr, indent);
        if (node->next) {
          fprintf(stream, ", ");
        }
        node = node->next;
      }
      fprintf(stream, "}");
    } break;

    case AST_EXPR_TYPE_MATCH:
      fprintf(stream, "match ");
      code_emit_expr(stream, ast->match.expr, indent);
      fprintf(stream, " {\n");
      struct ast_expr_match_arm *arm = ast->match.arms;
      while (arm) {
        print_indent(stream, indent + 1);
        code_emit_expr(stream, arm->pattern, indent);
        fprintf(stream, " => ");
        code_emit_expr(stream, arm->expr, indent);
        fprintf(stream, "\n");
        arm = arm->next;
      }
      if (ast->match.otherwise) {
        INDENTED(stream, indent + 1, "_ => ");
        code_emit_expr(stream, ast->match.otherwise->expr, indent);
        fprintf(stream, "\n");
      }
      INDENTED(stream, indent, "}");
      break;

    case AST_EXPR_TYPE_PATTERN_MATCH: {
      fprintf(stream, "enum %s::%s", ast->pattern_match.enum_name.value.identv.ident,
              ast->pattern_match.name.value.identv.ident);
      if (ast->pattern_match.inner_vdecl) {
        fprintf(stream, "(");
        code_emit_vdecl(stream, ast->pattern_match.inner_vdecl, 0);
        fprintf(stream, ")");
      } else if (ast->pattern_match.bindings_ignored) {
        fprintf(stream, "(_)");
      }
    } break;

    case AST_EXPR_TYPE_ENUM_INIT:
      fprintf(stream, "%s::%s", ast->enum_init.enum_ty_name.value.identv.ident,
              ast->enum_init.enum_val_name.value.identv.ident);
      if (ast->enum_init.inner) {
        fprintf(stream, "(");
        code_emit_expr(stream, ast->enum_init.inner, indent);
        fprintf(stream, ")");
      }
      break;

    default:
      fprintf(stream, "<unimpl-expr-%d>", ast->type);
      break;
  }
  return 0;
}

static int code_emit_ty(FILE *stream, struct ast_ty *ty) {
  if (type_is_tbd(ty)) {
    // don't render types pending inference
    return 0;
  }

  char buf[1024];
  type_name_into_as_code(ty, buf, 1024);
  fprintf(stream, "%s", buf);
  return 0;
}
