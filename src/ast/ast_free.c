#include <stdlib.h>

#include "ast.h"
#include "compiler.h"
#include "types.h"
#include "utility.h"

// Free a parser-generated type. These can have annoying heap pointers that aren't in the type
// repository and need to be freed.
static void free_parser_ty(struct compiler *compiler, struct ast_ty *ty);

void free_ast(struct compiler *compiler, struct ast_program *ast) {
  struct ast_toplevel *decl = ast->decls;
  while (decl) {
    struct ast_toplevel *next = decl->next;
    free_toplevel(compiler, decl);
    decl = next;
  }
}

void free_toplevel(struct compiler *compiler, struct ast_toplevel *ast) {
  if (ast->type == AST_DECL_TYPE_FDECL) {
    free_fdecl(compiler, &ast->fdecl, 0);
  } else if (ast->type == AST_DECL_TYPE_VDECL) {
    free_vdecl(compiler, &ast->vdecl, 0);
  } else if (ast->type == AST_DECL_TYPE_TYDECL) {
    free_tydecl(compiler, &ast->tydecl, 0);
  } else if (ast->type == AST_DECL_TYPE_PREPROC) {
    // nothing to be done here
  } else if (ast->type == AST_DECL_TYPE_IMPORT) {
    // nothing to be done here
  } else {
    fprintf(stderr, "unhandled free for toplevel type %d\n", ast->type);
  }

  free(ast);
}

void free_block(struct compiler *compiler, struct ast_block *ast, int heap) {
  struct ast_stmt *stmt = ast->stmt;
  while (stmt) {
    struct ast_stmt *next = stmt->next;
    free_stmt(compiler, stmt);
    stmt = next;
  }

  if (heap) {
    free(ast);
  }
}

void free_stmt(struct compiler *compiler, struct ast_stmt *ast) {
  switch (ast->type) {
    case AST_STMT_TYPE_EXPR:
      free_expr(compiler, ast->expr);
      break;

    case AST_STMT_TYPE_LET:
      free_vdecl(compiler, &ast->let, 0);
      break;

    case AST_STMT_TYPE_ITER:
      free_expr(compiler, ast->iter.range.start);
      free_expr(compiler, ast->iter.range.end);
      if (ast->iter.range.step) {
        free_expr(compiler, ast->iter.range.step);
      }
      free_block(compiler, &ast->iter.block, 0);
      if (ast->iter.index_vdecl) {
        free_vdecl(compiler, ast->iter.index_vdecl, 1);
      }
      break;

    case AST_STMT_TYPE_STORE:
      free_expr(compiler, ast->store.lhs);
      free_expr(compiler, ast->store.rhs);
      break;

    case AST_STMT_TYPE_RETURN:
      if (ast->expr) {
        free_expr(compiler, ast->expr);
      }
      break;

    case AST_STMT_TYPE_DEFER:
      free_expr(compiler, ast->expr);
      break;

    case AST_STMT_TYPE_WHILE:
      free_expr(compiler, ast->while_stmt.cond);
      free_block(compiler, &ast->while_stmt.block, 0);
      break;

    case AST_STMT_TYPE_CONTINUE:
    case AST_STMT_TYPE_BREAK:
      break;
  }

  free(ast);
}

void free_expr(struct compiler *compiler, struct ast_expr *ast) {
  if (!ast) {
    return;
  }

  switch (ast->type) {
    case AST_EXPR_TYPE_CONSTANT:
      switch (ast->parsed_ty.ty) {
        case AST_TYPE_FVEC:
        case AST_TYPE_ARRAY:
        case AST_TYPE_MATRIX:
          free_expr_list(compiler, ast->list);
          break;

        default:
          break;
      }
      break;

    case AST_EXPR_TYPE_STRUCT_INIT:
      free_expr_list(compiler, ast->list);
      break;

    case AST_EXPR_TYPE_BLOCK:
      free_block(compiler, &ast->block, 0);
      break;

    case AST_EXPR_TYPE_BINARY:
      free_expr(compiler, ast->binary.lhs);
      free_expr(compiler, ast->binary.rhs);
      break;

    case AST_EXPR_TYPE_VARIABLE:
      break;

    case AST_EXPR_TYPE_DEREF:
      free_expr(compiler, ast->deref.target);
      break;

    case AST_EXPR_TYPE_CALL: {
      free_expr_list(compiler, ast->call.args);
    } break;

    case AST_EXPR_TYPE_VOID:
      break;

    case AST_EXPR_TYPE_CAST:
      free_parser_ty(compiler, &ast->cast.parsed_ty);
      free_expr(compiler, ast->cast.expr);
      break;

    case AST_EXPR_TYPE_UNARY:
      free_expr(compiler, ast->unary.expr);
      break;

    case AST_EXPR_TYPE_IF:
      free_expr(compiler, ast->if_expr.cond);
      free_block(compiler, &ast->if_expr.then_block, 0);
      if (ast->if_expr.elseifs) {
        struct ast_expr_elseif *elseif = ast->if_expr.elseifs;
        while (elseif) {
          struct ast_expr_elseif *next = elseif->next;
          free_expr(compiler, elseif->cond);
          free_block(compiler, &elseif->block, 0);
          free(elseif);
          elseif = next;
        }
      }
      if (ast->if_expr.has_else) {
        free_block(compiler, &ast->if_expr.else_block, 0);
      }
      break;

    case AST_EXPR_TYPE_ASSIGN:
      free_expr(compiler, ast->assign.lhs);
      free_expr(compiler, ast->assign.expr);
      break;

    case AST_EXPR_TYPE_REF:
      free_expr(compiler, ast->ref.expr);
      break;

    case AST_EXPR_TYPE_LOAD:
      free_expr(compiler, ast->load.expr);
      break;

    case AST_EXPR_TYPE_ARRAY_INDEX:
      free_expr(compiler, ast->array_index.target);
      free_expr(compiler, ast->array_index.index);
      break;

    case AST_EXPR_TYPE_MATCH: {
      free_expr(compiler, ast->match.expr);
      struct ast_expr_match_arm *arm = ast->match.arms;
      while (arm) {
        struct ast_expr_match_arm *next = arm->next;
        free_expr(compiler, arm->pattern);
        free_expr(compiler, arm->expr);
        free(arm);
        arm = next;
      }

      if (ast->match.otherwise) {
        free_expr(compiler, ast->match.otherwise->expr);
        free(ast->match.otherwise);
      }
    } break;

    case AST_EXPR_TYPE_NIL:
      break;

    case AST_EXPR_TYPE_PATTERN_MATCH:
      if (ast->pattern_match.inner_vdecl) {
        free_vdecl(compiler, ast->pattern_match.inner_vdecl, 1);
      }
      break;

    case AST_EXPR_TYPE_ENUM_INIT:
      free_expr(compiler, ast->enum_init.inner);
      break;

    case AST_EXPR_TYPE_UNION_INIT:
      free_parser_ty(compiler, &ast->union_init.parsed_ty);
      free_expr(compiler, ast->union_init.inner);
      break;

    case AST_EXPR_TYPE_SIZEOF:
      if (ast->sizeof_expr.expr) {
        free_expr(compiler, ast->sizeof_expr.expr);
      } else {
        free_parser_ty(compiler, &ast->sizeof_expr.parsed_ty);
      }
      break;

    case AST_EXPR_TYPE_BOX:
    case AST_EXPR_TYPE_UNBOX:
      free_parser_ty(compiler, ast->box_expr.parsed_ty);
      if (ast->box_expr.expr) {
        free_expr(compiler, ast->box_expr.expr);
      }
      break;

    default:
      fprintf(stderr, "unhandled free for expr type %d\n", ast->type);
  }

  free_parser_ty(compiler, &ast->parsed_ty);
  free(ast);
}

void free_fdecl(struct compiler *compiler, struct ast_fdecl *ast, int heap) {
  if (ast->body) {
    free_block(compiler, ast->body, 1);
  }

  if (ast->params) {
    for (size_t i = 0; i < ast->num_params; i++) {
      free_vdecl(compiler, ast->params[i], 1);
    }

    free(ast->params);
  }

  if (ast->intrinsic_tys) {
    free(ast->intrinsic_tys);
  }

  free_parser_ty(compiler, &ast->parsed_retty);
  if (heap) {
    free(ast);
  }
}

void free_vdecl(struct compiler *compiler, struct ast_vdecl *ast, int heap) {
  if (ast->init_expr) {
    free_expr(compiler, ast->init_expr);
  }

  free_parser_ty(compiler, &ast->parser_ty);
  if (heap) {
    free(ast);
  }
}

void free_tydecl(struct compiler *compiler, struct ast_tydecl *ast, int heap) {
  free_parser_ty(compiler, &ast->parsed_ty);
  if (heap) {
    free(ast);
  }
}

void free_ty(struct compiler *compiler, struct ast_ty *ty, int heap) {
  /**
   * With the type repository, the only thing we need to free here is carriers of types.
   * Resolved types are shared, and should not be freed. When the type repository is
   * destroyed, the types themselves will all be freed.
   *
   * For example, an array of types needs to be freed, but the types can be left alone.
   */
  if (ty->specialization_of) {
    free(ty->specialization_of);
    ty->specialization_of = NULL;
  }

  if (ty->ty == AST_TYPE_STRUCT) {
    struct ast_struct_field *field = ty->structty.fields;
    while (field) {
      struct ast_struct_field *next = field->next;
      free(field);
      field = next;
    }

    ty->structty.fields = NULL;
  }

  if (ty->ty == AST_TYPE_ENUM) {
    struct ast_enum_field *field = ty->enumty.fields;
    while (field) {
      struct ast_enum_field *next = field->next;
      free(field);
      field = next;
    }

    struct ast_template_ty *template = ty->enumty.templates;
    while (template) {
      struct ast_template_ty *next = template->next;
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
      free(inner);
      inner = next;
    }

    ty->tmpl.inners = NULL;
  }

  if (ty->ty == AST_TYPE_FUNCTION) {
    free(ty->function.args);
  }

  if (ty->ty == AST_TYPE_POINTER || ty->ty == AST_TYPE_BOX) {
    ty->pointer.pointee = NULL;
  }

  if (!type_repository_is_shared_type(compiler_get_type_repository(compiler), ty)) {
    if (heap) {
      free(ty);
    }
  }
}

void free_parser_ty(struct compiler *compiler, struct ast_ty *ty) {
  if (ty->specialization_of) {
    free(ty->specialization_of);
    ty->specialization_of = NULL;
  }

  switch (ty->ty) {
    case AST_TYPE_ERROR:
    case AST_TYPE_TBD:
    case AST_TYPE_VOID:
    case AST_TYPE_INTEGER:
    case AST_TYPE_STRING:
    case AST_TYPE_FLOAT:
    case AST_TYPE_FVEC:
    case AST_TYPE_MATRIX:
    case AST_TYPE_CUSTOM:
    case AST_TYPE_NIL:
      // no complex data to free
      break;

    case AST_TYPE_ENUM: {
      struct ast_enum_field *field = ty->enumty.fields;
      while (field) {
        struct ast_enum_field *next = field->next;
        free_parser_ty(compiler, &field->parser_inner);
        free(field);
        field = next;
      }

      struct ast_template_ty *template = ty->enumty.templates;
      while (template) {
        struct ast_template_ty *next = template->next;
        free(template);
        template = next;
      }

      ty->enumty.fields = NULL;
      ty->enumty.templates = NULL;
    } break;

    case AST_TYPE_STRUCT: {
      struct ast_struct_field *field = ty->structty.fields;
      while (field) {
        struct ast_struct_field *next = field->next;
        free_parser_ty(compiler, &field->parsed_ty);
        free(field);
        field = next;
      }
    } break;

    case AST_TYPE_ARRAY:
      if (ty->array.element_ty) {
        free_parser_ty(compiler, ty->array.element_ty);
        free(ty->array.element_ty);
      }
      ty->array.element_ty = NULL;
      break;

    case AST_TYPE_TEMPLATE: {
      struct ast_template_ty *inner = ty->tmpl.inners;
      while (inner) {
        struct ast_template_ty *next = inner->next;
        free_parser_ty(compiler, &inner->parsed_ty);
        free(inner);
        inner = next;
      }

      ty->tmpl.inners = NULL;
    } break;

    case AST_TYPE_FUNCTION: {
      free_parser_ty(compiler, ty->function.retty);
      free(ty->function.retty);

      for (size_t i = 0; i < ty->function.num_args; i++) {
        free_parser_ty(compiler, ty->function.args[i]);
        free(ty->function.args[i]);
      }
      free(ty->function.args);
    } break;

    case AST_TYPE_POINTER:
    case AST_TYPE_BOX:
      free_parser_ty(compiler, ty->pointer.pointee);
      free(ty->pointer.pointee);
      ty->pointer.pointee = NULL;
      break;
  }
}

void free_expr_list(struct compiler *compiler, struct ast_expr_list *list) {
  struct ast_expr_list *node = list;
  while (node) {
    struct ast_expr_list *next = node->next;
    free_expr(compiler, node->expr);
    free(node);
    node = next;
  }
}
