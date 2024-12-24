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
  } else if (ast->type == AST_DECL_TYPE_IMPORT) {
    // nothing to be done here
  } else {
    fprintf(stderr, "unhandled free for toplevel type %d\n", ast->type);
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

  free_ty(&ast->ty, 0);
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
      if (ast->expr) {
        free_expr(ast->expr);
      }
      break;

    case AST_STMT_TYPE_DEFER:
      free_expr(ast->expr);
      break;

    case AST_STMT_TYPE_WHILE:
      free_expr(ast->while_stmt.cond);
      free_block(&ast->while_stmt.block, 0);
      break;

    case AST_STMT_TYPE_CONTINUE:
    case AST_STMT_TYPE_BREAK:
      break;
  }

  free(ast);
}

void free_expr(struct ast_expr *ast) {
  if (!ast) {
    return;
  }

  switch (ast->type) {
    case AST_EXPR_TYPE_CONSTANT:
      switch (ast->ty.ty) {
        case AST_TYPE_FVEC:
        case AST_TYPE_ARRAY:
        case AST_TYPE_MATRIX:
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
      free_expr(ast->deref.target);
      break;

    case AST_EXPR_TYPE_CALL: {
      free_expr_list(ast->call.args);
    } break;

    case AST_EXPR_TYPE_VOID:
      break;

    case AST_EXPR_TYPE_CAST:
      free_ty(&ast->cast.ty, 0);
      free_expr(ast->cast.expr);
      break;

    case AST_EXPR_TYPE_UNARY:
      free_expr(ast->unary.expr);
      break;

    case AST_EXPR_TYPE_IF:
      free_expr(ast->if_expr.cond);
      free_block(&ast->if_expr.then_block, 0);
      if (ast->if_expr.elseifs) {
        struct ast_expr_elseif *elseif = ast->if_expr.elseifs;
        while (elseif) {
          struct ast_expr_elseif *next = elseif->next;
          free_expr(elseif->cond);
          free_block(&elseif->block, 0);
          free(elseif);
          elseif = next;
        }
      }
      if (ast->if_expr.has_else) {
        free_block(&ast->if_expr.else_block, 0);
      }
      break;

    case AST_EXPR_TYPE_ASSIGN:
      free_expr(ast->assign.lhs);
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

    case AST_EXPR_TYPE_NIL:
      // always references an existing type that will be freed elsewhere
      free(ast);
      return;
      break;

    case AST_EXPR_TYPE_PATTERN_MATCH:
      if (ast->pattern_match.inner_vdecl) {
        free_vdecl(ast->pattern_match.inner_vdecl, 1);
      }
      break;

    case AST_EXPR_TYPE_ENUM_INIT:
      free_expr(ast->enum_init.inner);
      break;

    case AST_EXPR_TYPE_UNION_INIT:
      free_ty(&ast->union_init.ty, 0);
      free_expr(ast->union_init.inner);
      break;

    case AST_EXPR_TYPE_SIZEOF:
      if (ast->sizeof_expr.expr) {
        free_expr(ast->sizeof_expr.expr);
      } else {
        free_ty(&ast->sizeof_expr.ty, 0);
      }
      break;

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
  if (ty->specialization_of) {
    free(ty->specialization_of);
    ty->specialization_of = NULL;
  }

  if (ty->ty == AST_TYPE_ARRAY) {
    free_ty(ty->array.element_ty, 1);
    ty->array.element_ty = NULL;
  }

  if (ty->ty == AST_TYPE_STRUCT) {
    struct ast_struct_field *field = ty->structty.fields;
    while (field) {
      struct ast_struct_field *next = field->next;
      free_ty(field->ty, 1);
      free(field);
      field = next;
    }

    ty->structty.fields = NULL;
  }

  if (ty->ty == AST_TYPE_ENUM) {
    struct ast_enum_field *field = ty->enumty.fields;
    while (field) {
      struct ast_enum_field *next = field->next;
      if (field->has_inner) {
        free_ty(&field->inner, 0);
      }
      free(field);
      field = next;
    }

    struct ast_template_ty *template = ty->enumty.templates;
    while (template) {
      struct ast_template_ty *next = template->next;
      free_ty(&template->resolved, 0);
      free(template);
      template = next;
    }

    ty->enumty.fields = NULL;
    ty->enumty.templates = NULL;
  }

  if (ty->ty == AST_TYPE_TEMPLATE) {
    struct ast_template_ty *inner = ty->tmpl.inners;
    while (inner) {
      struct ast_template_ty *next = inner->next;
      free_ty(&inner->resolved, 0);
      free(inner);
      inner = next;
    }

    ty->tmpl.inners = NULL;

    free_ty(ty->tmpl.outer, 1);
  }

  if (ty->ty == AST_TYPE_FUNCTION) {
    free_ty(ty->function.retty, 1);
    for (size_t i = 0; i < ty->function.num_args; i++) {
      free_ty(ty->function.args[i], 1);
    }

    free(ty->function.args);
  }

  if (ty->ty == AST_TYPE_POINTER) {
    free_ty(ty->pointer.pointee, 1);
    ty->pointer.pointee = NULL;
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
