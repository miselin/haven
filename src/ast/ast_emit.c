/** Routines to emit an AST back as source code. */

#include <inttypes.h>
#include <stdio.h>

#include "ast.h"
#include "tokens.h"
#include "types.h"

struct code_emitter {
  // Used to put newlines between different types of top-level declarations.
  struct ast_toplevel *prev_toplevel;
};

static int code_emit_toplevel(struct code_emitter *code_emitter, FILE *stream,
                              struct ast_toplevel *ast);
static int code_emit_block(FILE *stream, struct ast_block *ast, int indent);
static int code_emit_stmt(FILE *stream, struct ast_stmt *ast, int indent);
static int code_emit_fdecl(FILE *stream, struct ast_fdecl *ast, int indent);
static int code_emit_vdecl(FILE *stream, struct ast_vdecl *ast, int indent);
static int code_emit_tydecl(FILE *stream, struct ast_tydecl *ast, int indent);
static int code_emit_expr(FILE *stream, struct ast_expr *ast, int indent);
static int code_emit_ty(FILE *stream, struct ast_ty *ast);

static void print_escaped(FILE *stream, const char *str);

int emit_ast_as_code(struct ast_program *ast, FILE *stream) {
  struct code_emitter emitter = {0};

  struct ast_toplevel *decl = ast->decls;
  while (decl) {
    if (code_emit_toplevel(&emitter, stream, decl) < 0) {
      return -1;
    }
    decl = decl->next;
  }
  fputs("\n", stream);
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

static int code_emit_toplevel(struct code_emitter *code_emitter, FILE *stream,
                              struct ast_toplevel *ast) {
  struct ast_toplevel *prev = code_emitter->prev_toplevel;
  code_emitter->prev_toplevel = ast;

  if (prev && prev->type != ast->type) {
    fprintf(stream, "\n");
  }

  if (ast->type == AST_DECL_TYPE_FDECL) {
    return code_emit_fdecl(stream, &ast->toplevel.fdecl, 0);
  } else if (ast->type == AST_DECL_TYPE_VDECL) {
    int rc = code_emit_vdecl(stream, &ast->toplevel.vdecl, 0);
    fprintf(stream, ";\n");
    return rc;
  } else if (ast->type == AST_DECL_TYPE_TYDECL) {
    return code_emit_tydecl(stream, &ast->toplevel.tydecl, 0);
  } else if (ast->type == AST_DECL_TYPE_PREPROC) {
    // no-op
  } else if (ast->type == AST_DECL_TYPE_IMPORT) {
    if (ast->toplevel.import.type == ImportTypeC) {
      fprintf(stream, "cimport ");
    } else {
      fprintf(stream, "import ");
    }
    fprintf(stream, "\"%s\";\n", ast->toplevel.import.path);
  } else {
    fprintf(stream, "<unhandled toplevel %d>\n", ast->type);
    return -1;
  }

  return 0;
}

static int code_emit_block(FILE *stream, struct ast_block *ast, int indent) {
  fprintf(stream, "{\n");
  struct ast_stmt *stmt = ast->stmt;
  while (stmt) {
    if (code_emit_stmt(stream, stmt, indent + 1) < 0) {
      return -1;
    }
    if (stmt->type == AST_STMT_TYPE_EXPR && stmt->stmt.expr->type == AST_EXPR_TYPE_VOID) {
      // print nothing for void expressions
    } else if (stmt->next || stmt->type != AST_STMT_TYPE_EXPR) {
      fprintf(stream, ";\n");
    } else {
      fprintf(stream, "\n");
    }
    stmt = stmt->next;
  }
  INDENTED(stream, indent, "}");
  return 0;
}

static int code_emit_stmt(FILE *stream, struct ast_stmt *ast, int indent) {
  switch (ast->type) {
    case AST_STMT_TYPE_EXPR:
      if (ast->stmt.expr->type == AST_EXPR_TYPE_VOID) {
        return 0;
      }
      print_indent(stream, indent);
      code_emit_expr(stream, ast->stmt.expr, indent);
      break;

    case AST_STMT_TYPE_LET:
      INDENTED(stream, indent, "let ");
      code_emit_vdecl(stream, &ast->stmt.let, indent);
      break;

    case AST_STMT_TYPE_ITER:
      INDENTED(stream, indent, "iter ");
      code_emit_expr(stream, ast->stmt.iter.range.start, indent);
      fprintf(stream, ":");
      code_emit_expr(stream, ast->stmt.iter.range.end, indent);
      if (ast->stmt.iter.range.step) {
        fprintf(stream, ":");
        code_emit_expr(stream, ast->stmt.iter.range.step, indent);
      }
      fprintf(stream, " ");
      // TODO: emit index variable
      code_emit_block(stream, &ast->stmt.iter.block, indent);
      break;

    case AST_STMT_TYPE_STORE:
      print_indent(stream, indent);
      code_emit_expr(stream, ast->stmt.store.lhs, indent);
      fprintf(stream, " = ");
      code_emit_expr(stream, ast->stmt.store.rhs, indent);
      break;

    case AST_STMT_TYPE_RETURN:
      if (ast->stmt.expr) {
        INDENTED(stream, indent, "ret%s", ast->stmt.expr->type == AST_EXPR_TYPE_VOID ? "" : " ");
        code_emit_expr(stream, ast->stmt.expr, indent);
      } else {
        INDENTED(stream, indent, "ret");
      }
      break;

    case AST_STMT_TYPE_DEFER:
      INDENTED(stream, indent, "defer ");
      code_emit_expr(stream, ast->stmt.expr, indent);
      break;

    case AST_STMT_TYPE_WHILE:
      INDENTED(stream, indent, "while ");
      code_emit_expr(stream, ast->stmt.while_stmt.cond, indent);
      fprintf(stream, " ");
      code_emit_block(stream, &ast->stmt.while_stmt.block, indent);
      break;

    case AST_STMT_TYPE_BREAK:
      INDENTED(stream, indent, "break");
      break;

    case AST_STMT_TYPE_CONTINUE:
      INDENTED(stream, indent, "continue");
      break;

    default:
      fprintf(stream, "<unhandled-stmt %d>\n", ast->type);
      return -1;
  }

  return 0;
}

static int code_emit_fdecl(FILE *stream, struct ast_fdecl *ast, int indent) {
  if (ast->flags & DECL_FLAG_PUB) {
    fprintf(stream, "pub ");
  }
  if (ast->flags & DECL_FLAG_IMPURE) {
    fprintf(stream, "impure ");
  }
  code_emit_ty(stream, ast->parsed_function_ty.oneof.function.retty);
  fprintf(stream, " fn %s(", ast->ident.value.identv.ident);
  for (size_t i = 0; i < ast->num_params; i++) {
    if (i > 0) {
      fprintf(stream, ", ");
    }
    // TODO
    // code_emit_vdecl(stream, ast->params[i], indent);
  }
  fprintf(stream, ")");
  if (ast->body) {
    fprintf(stream, " ");
    code_emit_block(stream, ast->body, indent);
    fprintf(stream, "\n\n");
    return 0;
  }

  if (ast->is_intrinsic) {
    fprintf(stream, " intrinsic \"%s\"%s", ast->intrinsic, ast->num_intrinsic_tys > 0 ? " " : "");

    for (size_t i = 0; i < ast->num_intrinsic_tys; i++) {
      code_emit_ty(stream, &ast->intrinsic_tys[i]);
      if (i + 1 < ast->num_intrinsic_tys) {
        fprintf(stream, ", ");
      }
    }
  }

  fprintf(stream, ";\n");
  return 0;
}

static int code_emit_vdecl(FILE *stream, struct ast_vdecl *ast, int indent) {
  struct ast_ty *ty = ast->ty ? ast->ty : &ast->parser_ty;

  if (!type_is_tbd(ty)) {
    code_emit_ty(stream, ty);
    fprintf(stream, " ");
  }
  fprintf(stream, "%s", ast->ident.value.identv.ident);

  if (ast->init_expr) {
    fprintf(stream, " = ");
    code_emit_expr(stream, ast->init_expr, indent);
  }
  return 0;
}

static int code_emit_tydecl(FILE *stream, struct ast_tydecl *ast, int indent) {
  INDENTED(stream, indent, "type %s = ", ast->ident.value.identv.ident);
  code_emit_ty(stream, &ast->parsed_ty);
  fprintf(stream, ";\n\n");
  return 0;
}

static void emit_opening(FILE *stream, struct ast_ty *ty) {
  char tyname[256];
  type_name_into(ty, tyname, 256);

  switch (ty->ty) {
    case AST_TYPE_ARRAY:
    case AST_TYPE_MATRIX:
      fprintf(stream, "%s {", tyname);
      break;

    case AST_TYPE_FVEC:
      fprintf(stream, "<");
      break;

    default:
      break;
  }
}

static void emit_closing(FILE *stream, struct ast_ty *ty) {
  switch (ty->ty) {
    case AST_TYPE_ARRAY:
    case AST_TYPE_MATRIX:
      fputs("}", stream);
      break;

    case AST_TYPE_FVEC:
      fputs(">", stream);
      break;

    default:
      break;
  }
}

static int code_emit_expr(FILE *stream, struct ast_expr *ast, int indent) {
  switch (ast->type) {
    case AST_EXPR_TYPE_CONSTANT: {
      if (ast->parsed_ty.ty == AST_TYPE_FVEC || ast->parsed_ty.ty == AST_TYPE_ARRAY ||
          ast->parsed_ty.ty == AST_TYPE_MATRIX) {
        emit_opening(stream, &ast->parsed_ty);
        struct ast_expr_list *node = ast->expr.list;
        while (node) {
          code_emit_expr(stream, node->expr, indent);
          if (node->next) {
            fprintf(stream, ", ");
          }
          node = node->next;
        }
        emit_closing(stream, &ast->parsed_ty);
      } else {
        switch (ast->expr.constant.constant.ident) {
          case TOKEN_INTEGER:
            fprintf(stream, "%" PRIi64, ast->expr.constant.constant.value.intv.val);
            break;

          case TOKEN_FLOAT:
            fprintf(stream, "%s", ast->expr.constant.constant.value.floatv.buf);
            break;

          case TOKEN_STRING:
            fputc('"', stream);
            print_escaped(stream, ast->expr.constant.constant.value.strv.s);
            fputc('"', stream);
            break;

          default:
            fprintf(stream, "<unimpl-constant-%d-or-%d>", ast->parsed_ty.ty,
                    ast->expr.constant.constant.ident);
            break;
        }
      }
    } break;

    case AST_EXPR_TYPE_BLOCK:
      code_emit_block(stream, &ast->expr.block, indent);
      break;

    case AST_EXPR_TYPE_BINARY:
      code_emit_expr(stream, ast->expr.binary.lhs, indent);
      fprintf(stream, " %s ", ast_binary_op_to_str(ast->expr.binary.op));
      code_emit_expr(stream, ast->expr.binary.rhs, indent);
      break;

    case AST_EXPR_TYPE_VARIABLE:
      fprintf(stream, "%s", ast->expr.variable.ident.value.identv.ident);
      break;

    case AST_EXPR_TYPE_DEREF:
      code_emit_expr(stream, ast->expr.deref.target, indent);
      if (ast->expr.deref.is_ptr) {
        fprintf(stream, "->%s", ast->expr.deref.field.value.identv.ident);
      } else {
        fprintf(stream, ".%s", ast->expr.deref.field.value.identv.ident);
      }
      break;

    case AST_EXPR_TYPE_CALL: {
      fprintf(stream, "%s(", ast->expr.call.ident.value.identv.ident);
      struct ast_expr_list *node = ast->expr.call.args;
      while (node) {
        code_emit_expr(stream, node->expr, indent);
        if (node->next) {
          fprintf(stream, ", ");
        }
        node = node->next;
      }
      fprintf(stream, ")");
    } break;

    case AST_EXPR_TYPE_VOID:
      break;

    case AST_EXPR_TYPE_CAST:
      fprintf(stream, "as ");
      code_emit_ty(stream, &ast->expr.cast.parsed_ty);
      fprintf(stream, " ");
      code_emit_expr(stream, ast->expr.cast.expr, indent);
      break;

    case AST_EXPR_TYPE_UNARY:
      fprintf(stream, "%s", ast_unary_op_to_str(ast->expr.unary.op));
      code_emit_expr(stream, ast->expr.unary.expr, indent);
      break;

    case AST_EXPR_TYPE_IF:
      fprintf(stream, "if ");
      code_emit_expr(stream, ast->expr.if_expr.cond, indent);
      fprintf(stream, " ");
      code_emit_block(stream, &ast->expr.if_expr.then_block, indent);
      if (ast->expr.if_expr.elseifs) {
        struct ast_expr_elseif *elseif = ast->expr.if_expr.elseifs;
        while (elseif) {
          fprintf(stream, " else if ");
          code_emit_expr(stream, elseif->cond, indent);
          fprintf(stream, " ");
          code_emit_block(stream, &elseif->block, indent);
          elseif = elseif->next;
        }
      }
      if (ast->expr.if_expr.has_else) {
        fprintf(stream, " else ");
        code_emit_block(stream, &ast->expr.if_expr.else_block, indent);
      }
      break;

    case AST_EXPR_TYPE_ASSIGN:
      code_emit_expr(stream, ast->expr.assign.lhs, indent);
      fprintf(stream, " = ");
      code_emit_expr(stream, ast->expr.assign.expr, indent);
      break;

    case AST_EXPR_TYPE_REF:
      fprintf(stream, "ref ");
      code_emit_expr(stream, ast->expr.ref.expr, indent);
      break;

    case AST_EXPR_TYPE_LOAD:
      fprintf(stream, "load ");
      code_emit_expr(stream, ast->expr.load.expr, indent);
      break;

    case AST_EXPR_TYPE_ARRAY_INDEX:
      code_emit_expr(stream, ast->expr.array_index.target, indent);
      fprintf(stream, "[");
      code_emit_expr(stream, ast->expr.array_index.index, indent);
      fprintf(stream, "]");
      break;

    case AST_EXPR_TYPE_MATCH:
      fprintf(stream, "match ");
      code_emit_expr(stream, ast->expr.match.expr, indent);
      fprintf(stream, " {\n");
      struct ast_expr_match_arm *arm = ast->expr.match.arms;
      while (arm) {
        print_indent(stream, indent + 1);
        code_emit_expr(stream, arm->pattern, indent);
        fprintf(stream, " => ");
        code_emit_expr(stream, arm->expr, indent);
        fprintf(stream, "\n");
        arm = arm->next;
      }
      if (ast->expr.match.otherwise) {
        INDENTED(stream, indent + 1, "_ => ");
        code_emit_expr(stream, ast->expr.match.otherwise->expr, indent);
        fprintf(stream, "\n");
      }
      INDENTED(stream, indent, "}");
      break;

    case AST_EXPR_TYPE_STRUCT_INIT: {
      fprintf(stream, "struct {");
      struct ast_expr_list *node = ast->expr.list;
      while (node) {
        code_emit_expr(stream, node->expr, indent);
        if (node->next) {
          fprintf(stream, ", ");
        }
        node = node->next;
      }
      fprintf(stream, "}");
    } break;

    case AST_EXPR_TYPE_NIL:
      fputs("nil", stream);
      break;

    case AST_EXPR_TYPE_PATTERN_MATCH: {
      fprintf(stream, "enum %s::%s", ast->expr.pattern_match.enum_name.value.identv.ident,
              ast->expr.pattern_match.name.value.identv.ident);
      if (ast->expr.pattern_match.inner_vdecl) {
        fprintf(stream, "(");
        code_emit_vdecl(stream, ast->expr.pattern_match.inner_vdecl, 0);
        fprintf(stream, ")");
      } else if (ast->expr.pattern_match.bindings_ignored) {
        fprintf(stream, "(_)");
      }
    } break;

    case AST_EXPR_TYPE_ENUM_INIT:
      fprintf(stream, "%s::%s", ast->expr.enum_init.enum_ty_name.value.identv.ident,
              ast->expr.enum_init.enum_val_name.value.identv.ident);
      if (ast->expr.enum_init.inner) {
        fprintf(stream, "(");
        code_emit_expr(stream, ast->expr.enum_init.inner, indent);
        fprintf(stream, ")");
      }
      break;

    case AST_EXPR_TYPE_SIZEOF:
      fprintf(stream, "sizeof (");
      if (ast->expr.sizeof_expr.expr) {
        code_emit_expr(stream, ast->expr.sizeof_expr.expr, indent);
      } else {
        code_emit_ty(stream, &ast->expr.sizeof_expr.parsed_ty);
      }
      fprintf(stream, ")");
      break;

    case AST_EXPR_TYPE_BOX:
      fprintf(stream, "box ");
      if (ast->expr.box_expr.expr) {
        code_emit_expr(stream, ast->expr.box_expr.expr, indent);
      } else {
        code_emit_ty(stream, &ast->expr.box_expr.parsed_ty);
      }
      break;

    case AST_EXPR_TYPE_UNBOX:
      fprintf(stream, "unbox ");
      code_emit_expr(stream, ast->expr.box_expr.expr, indent);
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

static void print_escaped(FILE *stream, const char *str) {
  while (*str) {
    if (*str == '\n') {
      fprintf(stream, "\\n");
    } else if (*str == '\t') {
      fprintf(stream, "\\t");
    } else {
      fprintf(stream, "%c", *str);
    }
    str++;
  }
}
