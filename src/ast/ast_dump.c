#include <stdio.h>

#include "ast.h"

#define INDENTED(level, ...)      \
  do {                            \
    print_indent(level);          \
    fprintf(stderr, __VA_ARGS__); \
  } while (0)

static void print_indent(int level) {
  for (int i = 0; i < level; i++) {
    fprintf(stderr, "  ");
  }
}

static void dump_toplevel(struct ast_toplevel *ast);
static void dump_block(struct ast_block *ast, int indent);
static void dump_stmt(struct ast_stmt *ast, int indent);
static void dump_expr(struct ast_expr *ast, int indent);
static void dump_fdecl(struct ast_fdecl *ast, int indent);
static void dump_vdecl(struct ast_vdecl *ast, int indent);

static void dump_ty(struct ast_ty *ty);
static void dump_decl_flags(int flags);

void dump_ast(struct ast_program *ast) {
  struct ast_toplevel *decl = ast->decls;
  while (decl) {
    dump_toplevel(decl);
    decl = decl->next;
  }
}

static void dump_toplevel(struct ast_toplevel *ast) {
  if (ast->is_fn) {
    dump_fdecl(&ast->fdecl, 0);
  } else {
    dump_vdecl(&ast->vdecl, 0);
  }

  fprintf(stderr, "\n");
}

static void dump_fdecl(struct ast_fdecl *ast, int indent) {
  INDENTED(indent, "Function %s [", ast->ident.value.identv.ident);
  dump_decl_flags(ast->flags);
  fprintf(stderr, "] (");
  for (size_t i = 0; i < ast->num_params; i++) {
    if (i > 0) {
      fprintf(stderr, ", ");
    }
    fprintf(stderr, "%s: ", ast->params[i]->ident.value.identv.ident);
    dump_ty(&ast->params[i]->ty);
  }
  fprintf(stderr, ") -> ");
  dump_ty(&ast->retty);

  if (!ast->body) {
    fprintf(stderr, ";");
  } else {
    fprintf(stderr, " ");
    dump_block(ast->body, indent);
  }
}

static void dump_vdecl(struct ast_vdecl *ast, int indent) {
  INDENTED(indent, "Var %s [", ast->ident.value.identv.ident);
  dump_decl_flags(ast->flags);
  fprintf(stderr, "]: ");
  dump_ty(&ast->ty);
  if (ast->init_expr) {
    fprintf(stderr, " = ");
    dump_expr(ast->init_expr, indent);
  }
  fprintf(stderr, ";");
}

static void dump_block(struct ast_block *ast, int indent) {
  fprintf(stderr, "{\n");
  struct ast_stmt *stmt = ast->stmt;
  while (stmt) {
    dump_stmt(stmt, indent + 2);
    stmt = stmt->next;
  }
  INDENTED(indent, "}");
}

static void dump_stmt(struct ast_stmt *ast, int indent) {
  switch (ast->type) {
    case AST_STMT_TYPE_EXPR:
      print_indent(indent);
      dump_expr(ast->expr, indent);
      break;

    case AST_STMT_TYPE_LET:
      dump_vdecl(&ast->let, indent);
      break;

    case AST_STMT_TYPE_ITER:
      INDENTED(indent, "Iter ");
      dump_expr(ast->iter.range.start, indent);
      fprintf(stderr, " to ");
      dump_expr(ast->iter.range.end, indent);
      if (ast->iter.range.step) {
        fprintf(stderr, " by ");
        dump_expr(ast->iter.range.step, indent);
      }
      fprintf(stderr, " for %s in ", ast->iter.index.ident.value.identv.ident);
      dump_block(&ast->iter.block, indent);
      break;

    case AST_STMT_TYPE_STORE:
      INDENTED(indent, "Store ");
      dump_expr(ast->store.lhs, indent);
      fprintf(stderr, ": ");
      dump_ty(&ast->store.lhs->ty);
      fprintf(stderr, " = ");
      dump_expr(ast->store.rhs, indent);
      fprintf(stderr, ": ");
      dump_ty(&ast->store.rhs->ty);
      break;

    case AST_STMT_TYPE_RETURN:
      INDENTED(indent, "Return ");
      dump_ty(&ast->expr->ty);
      fprintf(stderr, " ");
      dump_expr(ast->expr, indent);
      break;

    default:
      INDENTED(indent, "<unknown-stmt>\n");
  }

  fprintf(stderr, "\n");
}

static void dump_expr(struct ast_expr *ast, int indent) {
  switch (ast->type) {
    case AST_EXPR_TYPE_CONSTANT:
      switch (ast->ty.ty) {
        case AST_TYPE_INTEGER:
          fprintf(stderr, "%ld", ast->constant.constant.value.intv.val);
          break;

        case AST_TYPE_FLOAT:
          fprintf(stderr, "%s", ast->constant.constant.value.floatv.buf);
          break;

        case AST_TYPE_CHAR:
          fprintf(stderr, "'%c'", ast->constant.constant.value.charv.c);
          break;

        case AST_TYPE_STRING:
          fprintf(stderr, "\"%s\"", ast->constant.constant.value.strv.s);
          break;

        case AST_TYPE_FVEC: {
          fprintf(stderr, "<");
          struct ast_expr_list *node = ast->list;
          for (int i = 0; i < ast->ty.fvec.width; i++) {
            if (i > 0) {
              fprintf(stderr, ", ");
            }
            dump_expr(node->expr, indent);
            node = node->next;
          }
          fprintf(stderr, ">");
        } break;

        case AST_TYPE_ARRAY: {
          dump_ty(ast->ty.array.element_ty);
          fprintf(stderr, " {");
          struct ast_expr_list *node = ast->list;
          while (node) {
            dump_expr(node->expr, indent);
            node = node->next;
            if (node) {
              fprintf(stderr, ", ");
            }
          }
          fprintf(stderr, "}");
        } break;

        default:
          fprintf(stderr, "<unknown-constant>");
      }
      break;

    case AST_EXPR_TYPE_BLOCK:
      dump_block(&ast->block, indent);
      break;

    case AST_EXPR_TYPE_BINARY:
      fprintf(stderr, "Binary(%s, ", ast_binary_op_to_str(ast->binary.op));
      dump_expr(ast->binary.lhs, indent);
      fprintf(stderr, ", ");
      dump_expr(ast->binary.rhs, indent);
      fprintf(stderr, ") -> ");
      dump_ty(&ast->ty);
      break;

    case AST_EXPR_TYPE_VARIABLE:
      fprintf(stderr, "%s", ast->variable.ident.value.identv.ident);
      break;

    case AST_EXPR_TYPE_LOGICAL:
      fprintf(stderr, "Logical(%s, ", ast_logical_op_to_str(ast->logical.op));
      dump_expr(ast->logical.lhs, indent);
      fprintf(stderr, ", ");
      dump_expr(ast->logical.rhs, indent);
      fprintf(stderr, ") -> ");
      dump_ty(&ast->ty);
      break;

    case AST_EXPR_TYPE_LIST: {
      fprintf(stderr, "List ");
      dump_ty(&ast->ty);
      fprintf(stderr, " [");
      struct ast_expr_list *node = ast->list;
      while (node) {
        dump_expr(node->expr, indent);
        node = node->next;
        if (node) {
          fprintf(stderr, ", ");
        }
      }
      fprintf(stderr, "]");
    } break;

    case AST_EXPR_TYPE_DEREF:
      fprintf(stderr, "Deref(%s, %d) -> ", ast->deref.ident.value.identv.ident, ast->deref.field);
      dump_ty(&ast->ty);
      break;

    case AST_EXPR_TYPE_CALL: {
      fprintf(stderr, "Call(%s, ", ast->call.ident.value.identv.ident);
      struct ast_expr_list *node = ast->call.args;
      while (node) {
        dump_expr(node->expr, indent);
        node = node->next;
        if (node) {
          fprintf(stderr, ", ");
        }
      }
      fprintf(stderr, ") -> ");
      dump_ty(&ast->ty);
    } break;

    case AST_EXPR_TYPE_VOID:
      fprintf(stderr, "Void()");
      break;

    case AST_EXPR_TYPE_CAST:
      fprintf(stderr, "Cast(");
      dump_ty(&ast->cast.ty);
      fprintf(stderr, ", ");
      dump_expr(ast->cast.expr, indent);
      fprintf(stderr, ")");
      break;

    case AST_EXPR_TYPE_IF:
      fprintf(stderr, "If(");
      dump_expr(ast->if_expr.cond, indent);
      fprintf(stderr, ") ");
      dump_block(&ast->if_expr.then_block, indent);
      if (ast->if_expr.else_block.stmt) {
        fprintf(stderr, " else ");
        dump_block(&ast->if_expr.else_block, indent);
      }
      fprintf(stderr, " -> ");
      dump_ty(&ast->ty);
      break;

    case AST_EXPR_TYPE_ASSIGN:
      fprintf(stderr, "Assign(%s, ", ast->assign.ident.value.identv.ident);
      dump_expr(ast->assign.expr, indent);
      fprintf(stderr, ") -> ");
      dump_ty(&ast->ty);
      break;

    case AST_EXPR_TYPE_REF:
      fprintf(stderr, "Ref(");
      dump_expr(ast->ref.expr, indent);
      fprintf(stderr, ") -> ");
      dump_ty(&ast->ty);
      break;

    case AST_EXPR_TYPE_LOAD:
      fprintf(stderr, "Load(");
      dump_expr(ast->load.expr, indent);
      fprintf(stderr, ") -> ");
      dump_ty(&ast->ty);
      break;

    case AST_EXPR_TYPE_UNARY:
      fprintf(stderr, "Unary(%s, ", ast_unary_op_to_str(ast->unary.op));
      dump_expr(ast->unary.expr, indent);
      fprintf(stderr, ") -> ");
      dump_ty(&ast->ty);
      break;

    case AST_EXPR_TYPE_BOOLEAN:
      fprintf(stderr, "Boolean(%d, ", ast->boolean.op);
      dump_expr(ast->boolean.lhs, indent);
      fprintf(stderr, ", ");
      dump_expr(ast->boolean.rhs, indent);
      fprintf(stderr, ")");
      break;

    case AST_EXPR_TYPE_ARRAY_INDEX:
      fprintf(stderr, "ArrayIndex(%s, ", ast->array_index.ident.value.identv.ident);
      dump_expr(ast->array_index.index, indent);
      fprintf(stderr, ") -> ");
      dump_ty(&ast->ty);
      break;

    default:
      fprintf(stderr, "<unknown-expr %d>", ast->type);
  }
}

static void dump_ty(struct ast_ty *ty) {
  char buf[256];
  type_name_into(ty, buf, 256);
  fprintf(stderr, "%s", buf);
}

static void dump_maybe_space(const char *s, int first) {
  if (!first) {
    fputc(' ', stderr);
  }
  fputs(s, stderr);
}

static void dump_decl_flags(int flags) {
  int first = 1;
  if (flags & DECL_FLAG_PUB) {
    dump_maybe_space("pub", first);
    first = 0;
  }
  if (flags & DECL_FLAG_MUT) {
    dump_maybe_space("mut", first);
    first = 0;
  }
  if (flags & DECL_FLAG_VARARG) {
    dump_maybe_space("vararg", first);
    first = 0;
  }
  if (flags & DECL_FLAG_TEMPORARY) {
    dump_maybe_space("temporary", first);
    first = 0;
  }
}
