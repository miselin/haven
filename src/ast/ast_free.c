#include <malloc.h>

#include "ast.h"
#include "types.h"
#include "utility.h"

void free_ast(struct ast_program *ast) {
  struct ast_toplevel *decl = ast->decls;
  while (decl) {
    struct ast_toplevel *next = decl->next;
    free_toplevel(decl);
    decl = next;
  }
}

void free_toplevel(struct ast_toplevel *ast) {
  if (ast->type == AST_DECL_TYPE_FDECL) {
    free_fdecl(&ast->fdecl, 0);
  } else if (ast->type == AST_DECL_TYPE_VDECL) {
    free_vdecl(&ast->vdecl, 0);
  } else if (ast->type == AST_DECL_TYPE_TYDECL) {
    free_tydecl(&ast->tydecl, 0);
  } else if (ast->type == AST_DECL_TYPE_PREPROC) {
    // nothing to be done here
  }

  free(ast);
}

void free_block(struct ast_block *ast, int heap) {
  struct ast_stmt *stmt = ast->stmt;
  while (stmt) {
    struct ast_stmt *next = stmt->next;
    free_stmt(stmt);
    stmt = next;
  }

  if (heap) {
    free(ast);
  }
}

void free_stmt(struct ast_stmt *ast) {
  switch (ast->type) {
    case AST_STMT_TYPE_EXPR:
      free_expr(ast->expr);
      break;

    case AST_STMT_TYPE_LET:
      free_vdecl(&ast->let, 0);
      break;

    case AST_STMT_TYPE_ITER:
      free_expr(ast->iter.range.start);
      free_expr(ast->iter.range.end);
      if (ast->iter.range.step) {
        free_expr(ast->iter.range.step);
      }
      free_block(&ast->iter.block, 0);
      if (ast->iter.index_vdecl) {
        free_vdecl(ast->iter.index_vdecl, 1);
      }
      break;

    case AST_STMT_TYPE_STORE:
      free_expr(ast->store.lhs);
      free_expr(ast->store.rhs);
      break;

    case AST_STMT_TYPE_RETURN:
      free_expr(ast->expr);
      break;
  }

  free(ast);
}

void free_expr(struct ast_expr *ast) {
  switch (ast->type) {
    case AST_EXPR_TYPE_CONSTANT:
      switch (ast->ty.ty) {
        case AST_TYPE_FVEC:
        case AST_TYPE_ARRAY:
          free_expr_list(ast->list);
          break;

        default:
          break;
      }
      break;

    case AST_EXPR_TYPE_STRUCT_INIT:
      free_expr_list(ast->list);
      break;

    case AST_EXPR_TYPE_BLOCK:
      free_block(&ast->block, 0);
      break;

    case AST_EXPR_TYPE_BINARY:
      free_expr(ast->binary.lhs);
      free_expr(ast->binary.rhs);
      break;

    case AST_EXPR_TYPE_VARIABLE:
      break;

    case AST_EXPR_TYPE_LOGICAL:
      free_expr(ast->logical.lhs);
      free_expr(ast->logical.rhs);
      break;

    case AST_EXPR_TYPE_LIST: {
      free_expr_list(ast->list);
    } break;

    case AST_EXPR_TYPE_DEREF:
      break;

    case AST_EXPR_TYPE_CALL: {
      free_expr_list(ast->call.args);
    } break;

    case AST_EXPR_TYPE_VOID:
      break;

    case AST_EXPR_TYPE_CAST:
      free_expr(ast->cast.expr);
      break;

    case AST_EXPR_TYPE_UNARY:
      free_expr(ast->unary.expr);
      break;

    case AST_EXPR_TYPE_IF:
      free_expr(ast->if_expr.cond);
      free_block(&ast->if_expr.then_block, 0);
      if (ast->if_expr.has_else) {
        free_block(&ast->if_expr.else_block, 0);
      }
      break;

    case AST_EXPR_TYPE_ASSIGN:
      free_expr(ast->assign.expr);
      break;

    case AST_EXPR_TYPE_REF:
      free_expr(ast->ref.expr);
      break;

    case AST_EXPR_TYPE_LOAD:
      free_expr(ast->load.expr);
      break;

    case AST_EXPR_TYPE_BOOLEAN:
      free_expr(ast->boolean.lhs);
      free_expr(ast->boolean.rhs);
      break;

    case AST_EXPR_TYPE_ARRAY_INDEX:
      free_expr(ast->array_index.index);
      break;

    case AST_EXPR_TYPE_MATCH: {
      free_expr(ast->match.expr);
      struct ast_expr_match_arm *arm = ast->match.arms;
      while (arm) {
        struct ast_expr_match_arm *next = arm->next;
        free_expr(arm->pattern);
        free_expr(arm->expr);
        free(arm);
        arm = next;
      }

      if (ast->match.otherwise) {
        free_expr(ast->match.otherwise->expr);
        free(ast->match.otherwise);
      }
    } break;

    default:
      fprintf(stderr, "unhandled free for expr type %d\n", ast->type);
  }

  free_ty(&ast->ty, 0);
  free(ast);
}

void free_fdecl(struct ast_fdecl *ast, int heap) {
  if (ast->body) {
    free_block(ast->body, 1);
  }

  if (ast->params) {
    for (size_t i = 0; i < ast->num_params; i++) {
      free_vdecl(ast->params[i], 1);
    }

    free(ast->params);
  }

  free_ty(&ast->retty, 0);
  if (heap) {
    free(ast);
  }
}

void free_vdecl(struct ast_vdecl *ast, int heap) {
  if (ast->init_expr) {
    free_expr(ast->init_expr);
  }

  free_ty(&ast->ty, 0);
  if (heap) {
    free(ast);
  }
}

void free_tydecl(struct ast_tydecl *ast, int heap) {
  free_ty(&ast->ty, 0);
  if (heap) {
    free(ast);
  }
}

void free_ty(struct ast_ty *ty, int heap) {
  // only free internals of a type if this ast_ty instance owns them
  if ((ty->flags & TYPE_FLAG_INDIRECT) == 0) {
    if (ty->ty == AST_TYPE_ARRAY) {
      free_ty(ty->array.element_ty, 1);
    }

    if (ty->ty == AST_TYPE_STRUCT) {
      struct ast_struct_field *field = ty->structty.fields;
      while (field) {
        struct ast_struct_field *next = field->next;
        free_ty(field->ty, 1);
        free(field);
        field = next;
      }
    }
  }

  if (heap) {
    free(ty);
  }
}

void free_expr_list(struct ast_expr_list *list) {
  struct ast_expr_list *node = list;
  while (node) {
    struct ast_expr_list *next = node->next;
    free_expr(node->expr);
    free(node);
    node = next;
  }
}
