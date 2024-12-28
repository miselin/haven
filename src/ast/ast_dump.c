#include <inttypes.h>
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
static void dump_fdecl(struct ast_fdecl *ast, int indent);
static void dump_vdecl(struct ast_vdecl *ast, int indent);
static void dump_tydecl(struct ast_tydecl *ast, int indent);
static void dump_match_arms(struct ast_expr_match_arm *arm, int indent);
static void dump_match_arm(struct ast_expr_match_arm *arm, int indent);
static void dump_array(struct ast_expr *ast, int indent);

static void dump_ty(struct ast_ty *ty);
static void dump_decl_flags(uint64_t flags);

// Dumps the right type of an expression depending on whether it's been resolved yet or not.
// AST dumping can happen before typecheck so we need to handle both cases.
static void dump_expr_ty(struct ast_expr *ast);

void dump_ast(struct ast_program *ast) {
  struct ast_toplevel *decl = ast->decls;
  while (decl) {
    dump_toplevel(decl);
    decl = decl->next;
  }
}

static void dump_toplevel(struct ast_toplevel *ast) {
  if (ast->type == AST_DECL_TYPE_FDECL) {
    dump_fdecl(&ast->fdecl, 0);
  } else if (ast->type == AST_DECL_TYPE_VDECL) {
    dump_vdecl(&ast->vdecl, 0);
  } else if (ast->type == AST_DECL_TYPE_TYDECL) {
    dump_tydecl(&ast->tydecl, 0);
  } else if (ast->type == AST_DECL_TYPE_PREPROC) {
    fprintf(stderr, "PreprocessorDecl\n");
  } else if (ast->type == AST_DECL_TYPE_IMPORT) {
    fprintf(stderr, "ImportDecl\n");
  } else {
    fprintf(stderr, "<unknown-toplevel>\n");
  }
}

static void dump_fdecl(struct ast_fdecl *ast, int indent) {
  INDENTED(indent, "\nFunctionDecl %s [", ast->ident.value.identv.ident);
  dump_decl_flags(ast->flags);
  fprintf(stderr, "] -> ");
  dump_ty(ast->retty);
  fprintf(stderr, "\n");

  if (ast->num_params) {
    INDENTED(indent + 1, "Params:\n");
    for (size_t i = 0; i < ast->num_params; i++) {
      INDENTED(indent + 2, "%s: ", ast->params[i]->ident.value.identv.ident);
      dump_ty(ast->params[i]->ty);
      fprintf(stderr, "\n");
    }
  }

  if (ast->body) {
    dump_block(ast->body, indent + 1);
  }

  if (ast->is_intrinsic) {
    INDENTED(indent + 1, "Intrinsic %s\n", ast->intrinsic);
    for (size_t i = 0; i < ast->num_intrinsic_tys; i++) {
      INDENTED(indent + 2, "Intrinsic Overload %zu -> ", i);
      dump_ty(&ast->intrinsic_tys[i]);
      fprintf(stderr, "\n");
    }
  }
}

static void dump_vdecl(struct ast_vdecl *ast, int indent) {
  INDENTED(indent, "VariableDecl %s [", ast->ident.value.identv.ident);
  dump_decl_flags(ast->flags);
  fprintf(stderr, "] -> ");
  dump_ty(ast->ty);
  fprintf(stderr, "\n");

  if (ast->init_expr) {
    dump_expr(ast->init_expr, indent + 1);
  }
}

static void dump_tydecl(struct ast_tydecl *ast, int indent) {
  INDENTED(indent, "TypeDecl %s\n", ast->ident.value.identv.ident);
  print_indent(indent + 1);
  dump_ty(&ast->parsed_ty);

  if (ast->resolved) {
    fprintf(stderr, " -> ");
    dump_ty(ast->resolved);
  }

  fprintf(stderr, "\n");
}

static void dump_block(struct ast_block *ast, int indent) {
  INDENTED(indent, "Block -> ");
  dump_ty(ast->ty);
  fprintf(stderr, "\n");

  struct ast_stmt *stmt = ast->stmt;
  while (stmt) {
    dump_stmt(stmt, indent + 1);
    stmt = stmt->next;
  }
}

static void dump_stmt(struct ast_stmt *ast, int indent) {
  switch (ast->type) {
    case AST_STMT_TYPE_EXPR:
      dump_expr(ast->expr, indent);
      break;

    case AST_STMT_TYPE_LET:
      dump_vdecl(&ast->let, indent);
      break;

    case AST_STMT_TYPE_ITER:
      INDENTED(indent, "Iter\n");
      INDENTED(indent + 1, "Range\n");
      dump_expr(ast->iter.range.start, indent + 2);
      dump_expr(ast->iter.range.end, indent + 2);
      if (ast->iter.range.step) {
        dump_expr(ast->iter.range.step, indent + 2);
      }
      INDENTED(indent + 2, "%s -> ", ast->iter.index.ident.value.identv.ident);
      if (ast->iter.index_vdecl) {
        dump_ty(&ast->iter.index_vdecl->parser_ty);
      } else {
        fprintf(stderr, "<invalid-type>");
      }
      fprintf(stderr, "\n");

      dump_block(&ast->iter.block, indent + 3);
      break;

    case AST_STMT_TYPE_STORE:
      INDENTED(indent, "Store\n");
      dump_expr(ast->store.lhs, indent + 1);
      dump_expr(ast->store.rhs, indent + 1);
      break;

    case AST_STMT_TYPE_RETURN:
      if (ast->expr) {
        INDENTED(indent, "Return -> ");
        dump_ty(ast->expr->ty);
        fprintf(stderr, "\n");
        dump_expr(ast->expr, indent + 1);
      } else {
        INDENTED(indent, "Return\n");
      }
      break;

    case AST_STMT_TYPE_DEFER: {
      INDENTED(indent, "Defer\n");
      dump_expr(ast->expr, indent + 1);
    } break;

    case AST_STMT_TYPE_WHILE:
      INDENTED(indent, "While\n");
      INDENTED(indent + 1, "Cond: ");
      dump_expr(ast->while_stmt.cond, indent);
      fprintf(stderr, "\n");
      dump_block(&ast->while_stmt.block, indent + 1);
      break;

    case AST_STMT_TYPE_BREAK:
      INDENTED(indent, "Break\n");
      break;

    case AST_STMT_TYPE_CONTINUE:
      INDENTED(indent, "Continue\n");
      break;

    default:
      INDENTED(indent, "<unknown-stmt>\n");
  }
}

void dump_expr(struct ast_expr *ast, int indent) {
  if (!ast) {
    return;
  }

  switch (ast->type) {
    case AST_EXPR_TYPE_CONSTANT: {
      INDENTED(indent, "Constant ");
      dump_expr_ty(ast);
      fprintf(stderr, "\n");

      struct ast_ty *ty = ast->ty ? ast->ty : &ast->parsed_ty;
      switch (ty->ty) {
        case AST_TYPE_INTEGER:
          INDENTED(indent + 1, "Integer %" PRIi64, ast->constant.constant.value.intv.val);
          break;

        case AST_TYPE_FLOAT:
          INDENTED(indent + 1, "Float %s", ast->constant.constant.value.floatv.buf);
          break;

        case AST_TYPE_STRING:
          INDENTED(indent + 1, "String \"%s\"", ast->constant.constant.value.strv.s);
          break;

        case AST_TYPE_FVEC: {
          INDENTED(indent + 1, "FVec\n");
          struct ast_expr_list *node = ast->list;
          for (size_t i = 0; i < ty->fvec.width; i++) {
            dump_expr(node->expr, indent + 2);
            node = node->next;
          }
        } break;

        case AST_TYPE_ARRAY: {
          INDENTED(indent + 1, "Array\n");
          dump_array(ast, indent + 2);
        } break;

        case AST_TYPE_MATRIX: {
          fprintf(stderr, "Matrix%zdx%zd\n", ty->matrix.cols, ty->matrix.rows);
          struct ast_expr_list *node = ast->list;
          for (size_t i = 0; i < ty->matrix.rows; i++) {
            dump_expr(node->expr, indent + 2);
            node = node->next;
          }
        } break;

        default:
          fprintf(stderr, "<unknown-constant>");
      }

      fprintf(stderr, "\n");
    } break;

    case AST_EXPR_TYPE_STRUCT_INIT:
      INDENTED(indent, "StructInit\n");
      dump_array(ast, indent + 1);
      break;

    case AST_EXPR_TYPE_BLOCK:
      dump_block(&ast->block, indent);
      break;

    case AST_EXPR_TYPE_BINARY:
      INDENTED(indent, "BinaryExpr %s -> ", ast_binary_op_to_str(ast->binary.op));
      dump_expr_ty(ast);
      fprintf(stderr, "\n");

      dump_expr(ast->binary.lhs, indent + 1);
      dump_expr(ast->binary.rhs, indent + 1);
      break;

    case AST_EXPR_TYPE_VARIABLE:
      INDENTED(indent, "VariableExpr %s -> ", ast->variable.ident.value.identv.ident);
      dump_expr_ty(ast);
      fprintf(stderr, "\n");
      break;

    case AST_EXPR_TYPE_DEREF:
      INDENTED(indent, "Deref %s [#%zd] -> ", ast->deref.field.value.identv.ident,
               ast->deref.field_idx);
      dump_expr_ty(ast);
      fprintf(stderr, "\n");

      dump_expr(ast->deref.target, indent + 1);
      break;

    case AST_EXPR_TYPE_CALL: {
      INDENTED(indent, "Call %s -> ", ast->call.ident.value.identv.ident);
      dump_expr_ty(ast);
      fprintf(stderr, "\n");

      struct ast_expr_list *node = ast->call.args;
      if (node) {
        INDENTED(indent + 1, "Args\n");
        while (node) {
          dump_expr(node->expr, indent + 2);
          node = node->next;
        }
      }
    } break;

    case AST_EXPR_TYPE_VOID:
      INDENTED(indent, "Void\n");
      break;

    case AST_EXPR_TYPE_CAST:
      INDENTED(indent, "Cast -> ");
      dump_ty(&ast->cast.parsed_ty);
      fprintf(stderr, "\n");

      dump_expr(ast->cast.expr, indent + 1);
      break;

    case AST_EXPR_TYPE_IF:
      INDENTED(indent, "If [has_else=%d] -> ", ast->if_expr.has_else);
      dump_expr_ty(ast);
      fprintf(stderr, " \n");

      dump_expr(ast->if_expr.cond, indent + 1);
      dump_block(&ast->if_expr.then_block, indent + 1);

      if (ast->if_expr.elseifs) {
        struct ast_expr_elseif *elseif = ast->if_expr.elseifs;
        while (elseif) {
          INDENTED(indent + 1, "ElseIf\n");
          dump_expr(elseif->cond, indent + 2);
          dump_block(&elseif->block, indent + 2);
          elseif = elseif->next;
        }
      }

      if (ast->if_expr.else_block.stmt) {
        dump_block(&ast->if_expr.else_block, indent + 1);
      }
      break;

    case AST_EXPR_TYPE_ASSIGN:
      INDENTED(indent, "Assign -> ");
      dump_expr_ty(ast);
      fprintf(stderr, "\n");

      dump_expr(ast->assign.lhs, indent + 1);
      dump_expr(ast->assign.expr, indent + 1);
      break;

    case AST_EXPR_TYPE_REF: {
      INDENTED(indent, "Ref -> ");
      dump_expr_ty(ast);
      fprintf(stderr, "\n");

      dump_expr(ast->ref.expr, indent + 1);
    } break;

    case AST_EXPR_TYPE_LOAD: {
      INDENTED(indent, "Load -> ");
      dump_expr_ty(ast);
      fprintf(stderr, "\n");

      dump_expr(ast->load.expr, indent + 1);
    } break;

    case AST_EXPR_TYPE_UNARY: {
      INDENTED(indent, "Unary %s -> ", ast_unary_op_to_str(ast->unary.op));
      dump_expr_ty(ast);
      fprintf(stderr, "\n");

      dump_expr(ast->unary.expr, indent + 1);
    } break;

    case AST_EXPR_TYPE_ARRAY_INDEX: {
      INDENTED(indent, "ArrayIndex -> ");
      dump_expr_ty(ast);
      fprintf(stderr, "\n");

      dump_expr(ast->array_index.target, indent + 1);
      dump_expr(ast->array_index.index, indent + 1);
    } break;

    case AST_EXPR_TYPE_MATCH: {
      INDENTED(indent, "Match -> ");
      dump_expr_ty(ast);
      fprintf(stderr, "\n");

      dump_expr(ast->match.expr, indent + 1);

      dump_match_arms(ast->match.arms, indent + 1);

      if (ast->match.otherwise) {
        INDENTED(indent + 1, "Otherwise\n");
        dump_expr(ast->match.otherwise->expr, indent + 2);
      }
    } break;

    case AST_EXPR_TYPE_NIL:
      INDENTED(indent, "nil\n");
      break;

    case AST_EXPR_TYPE_PATTERN_MATCH: {
      INDENTED(indent, "PatternMatch %s -> ", ast->pattern_match.name.value.identv.ident);
      dump_expr_ty(ast);
      fprintf(stderr, "\n");

      if (ast->pattern_match.inner_vdecl) {
        // fprintf(stderr, "%s", ast->pattern_match.inner.value.identv.ident);
        dump_vdecl(ast->pattern_match.inner_vdecl, indent + 1);
      } else {
        INDENTED(indent + 1, "_");
      }
    } break;

    case AST_EXPR_TYPE_ENUM_INIT: {
      INDENTED(indent, "EnumInit %s %s -> ", ast->enum_init.enum_ty_name.value.identv.ident,
               ast->enum_init.enum_val_name.value.identv.ident);
      dump_expr_ty(ast);
      fprintf(stderr, "\n");

      if (ast->enum_init.inner) {
        dump_expr(ast->enum_init.inner, indent + 1);
      }
    } break;

    case AST_EXPR_TYPE_UNION_INIT:
      INDENTED(indent, "UnionInit %s -> ", ast->union_init.field.value.identv.ident);
      dump_ty(&ast->union_init.parsed_ty);
      fprintf(stderr, "\n");

      dump_expr(ast->union_init.inner, indent + 1);
      break;

    case AST_EXPR_TYPE_SIZEOF: {
      INDENTED(indent, "Sizeof -> ");
      dump_expr_ty(ast);
      fprintf(stderr, "\n");

      if (ast->sizeof_expr.expr) {
        dump_expr(ast->sizeof_expr.expr, indent + 1);
      } else {
        INDENTED(indent + 1, "Type ");
        dump_ty(&ast->sizeof_expr.parsed_ty);
        fprintf(stderr, "\n");
      }
    } break;

    case AST_EXPR_TYPE_BOX:
    case AST_EXPR_TYPE_UNBOX: {
      INDENTED(indent, "%s -> ", ast->type == AST_EXPR_TYPE_BOX ? "Box" : "Unbox");
      dump_expr_ty(ast);
      fprintf(stderr, "\n");

      if (ast->box_expr.parsed_ty) {
        dump_ty(ast->box_expr.parsed_ty);
      }
      if (ast->box_expr.expr) {
        dump_expr(ast->box_expr.expr, indent + 1);
      }
    } break;

    default:
      INDENTED(indent, "<unknown-expr %d>\n", ast->type);
  }
}

static void dump_match_arms(struct ast_expr_match_arm *arm, int indent) {
  while (arm) {
    dump_match_arm(arm, indent);
    arm = arm->next;
  }
}

static void dump_match_arm(struct ast_expr_match_arm *arm, int indent) {
  INDENTED(indent, "MatchArm\n");
  if (arm->pattern) {
    dump_expr(arm->pattern, indent + 1);
  } else {
    INDENTED(indent + 1, "_");
  }
  dump_expr(arm->expr, indent + 1);
}

static void dump_ty(struct ast_ty *ty) {
  fprintf(stderr, "ty %p: ", (void *)ty);

  char buf[1024];
  type_name_into(ty, buf, 1024);
  fprintf(stderr, "%s", buf);
}

static void dump_maybe_space(const char *s, int first) {
  if (!first) {
    fputc(' ', stderr);
  }
  fputs(s, stderr);
}

static void dump_decl_flags(uint64_t flags) {
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
  if (flags & DECL_FLAG_IMPURE) {
    dump_maybe_space("impure", first);
    first = 0;
  }
}

static void dump_array(struct ast_expr *ast, int indent) {
  INDENTED(indent, "Array -> ");
  struct ast_ty *ty = ast->ty ? ast->ty : &ast->parsed_ty;
  dump_ty(ty->array.element_ty);
  fprintf(stderr, "\n");

  struct ast_expr_list *node = ast->list;
  while (node) {
    dump_expr(node->expr, indent + 1);
    node = node->next;
  }
}

static void dump_expr_ty(struct ast_expr *ast) {
  struct ast_ty *ty = ast->ty ? ast->ty : &ast->parsed_ty;
  dump_ty(ty);
}
