#include "ast.h"
#include "compiler.h"

struct ast_visitor {
  struct compiler *compiler;
  ast_visitor_fn visit;
  void *user_data;
  size_t depth;
};

static int visitor_visit_ast(struct ast_visitor *visitor, struct ast_program *ast);
static enum VisitorResult visitor_visit_toplevel(struct ast_visitor *visitor,
                                                 struct ast_toplevel *ast);
static enum VisitorResult visitor_visit_block(struct ast_visitor *visitor, struct ast_block *ast);
static enum VisitorResult visitor_visit_stmt(struct ast_visitor *visitor, struct ast_stmt *ast);
static enum VisitorResult visitor_visit_expr(struct ast_visitor *visitor, struct ast_expr *ast);

void ast_visit(struct compiler *compiler, struct ast_program *ast, ast_visitor_fn visit,
               void *user_data) {
  struct ast_visitor visitor = {compiler, visit, user_data, 0};
  visitor_visit_ast(&visitor, ast);
}

static int visitor_visit_ast(struct ast_visitor *visitor, struct ast_program *ast) {
  struct ast_toplevel *decl = ast->decls;
  while (decl) {
    enum VisitorResult result = visitor_visit_toplevel(visitor, decl);
    if (result == VisitorStop) {
      return 0;
    }
    decl = decl->next;
  }
  return 0;
}

static enum VisitorResult visitor_visit_toplevel(struct ast_visitor *visitor,
                                                 struct ast_toplevel *ast) {
  compiler_log(visitor->compiler, LogLevelTrace, "ast_visitor", "visiting toplevel %d [depth=%zd]",
               ast->type, visitor->depth);

  // TODO: vdecl init_exprs, fdecl bodies, etc
  struct ast_visitor_node node = {.toplevel = ast, .depth = visitor->depth++};
  enum VisitorResult result = visitor->visit(&node, visitor->user_data);
  --visitor->depth;

  if (result == VisitorStop || result == VisitorSkipChildren) {
    return result;
  }

  switch (ast->type) {
    case AST_DECL_TYPE_FDECL:
      if (ast->fdecl.body && visitor_visit_block(visitor, ast->fdecl.body) == VisitorStop) {
        return VisitorStop;
      }
      break;

    case AST_DECL_TYPE_VDECL:
      if (ast->vdecl.init_expr &&
          visitor_visit_expr(visitor, ast->vdecl.init_expr) == VisitorStop) {
        return VisitorStop;
      }
      break;

    case AST_DECL_TYPE_TYDECL:
      break;

    case AST_DECL_TYPE_PREPROC:
      break;

    case AST_DECL_TYPE_IMPORT:
      break;

    default:
      compiler_log(visitor->compiler, LogLevelError, "ast_visitor", "unhandled toplevel type %d",
                   ast->type);
  }

  return VisitorContinue;
}

static enum VisitorResult visitor_visit_block(struct ast_visitor *visitor, struct ast_block *ast) {
  compiler_log(visitor->compiler, LogLevelTrace, "ast_visitor", "visiting block [depth=%zd]",
               visitor->depth);

  struct ast_visitor_node node = {.block = ast, .depth = visitor->depth++};
  enum VisitorResult result = visitor->visit(&node, visitor->user_data);
  --visitor->depth;
  if (result == VisitorStop || result == VisitorSkipChildren) {
    return result;
  }

  struct ast_stmt *stmt = ast->stmt;
  while (stmt) {
    if (visitor_visit_stmt(visitor, stmt) == VisitorStop) {
      return VisitorStop;
    }
    stmt = stmt->next;
  }
  return VisitorContinue;
}

static enum VisitorResult visitor_visit_stmt(struct ast_visitor *visitor, struct ast_stmt *ast) {
  compiler_log(visitor->compiler, LogLevelTrace, "ast_visitor", "visiting statement %d [depth=%zd]",
               ast->type, visitor->depth);

  struct ast_visitor_node node = {.stmt = ast, .depth = visitor->depth++};
  enum VisitorResult result = visitor->visit(&node, visitor->user_data);
  --visitor->depth;
  if (result == VisitorStop || result == VisitorSkipChildren) {
    return result;
  }

  switch (ast->type) {
    case AST_STMT_TYPE_EXPR:
      return visitor_visit_expr(visitor, ast->expr);

    case AST_STMT_TYPE_LET:
      if (ast->let.init_expr) {
        return visitor_visit_expr(visitor, ast->let.init_expr);
      }
      break;

    case AST_STMT_TYPE_ITER:
      if (visitor_visit_expr(visitor, ast->iter.range.start) == VisitorStop) {
        return VisitorStop;
      }
      if (visitor_visit_expr(visitor, ast->iter.range.end) == VisitorStop) {
        return VisitorStop;
      }
      if (ast->iter.range.step &&
          visitor_visit_expr(visitor, ast->iter.range.step) == VisitorStop) {
        return VisitorStop;
      }

      if (visitor_visit_block(visitor, &ast->iter.block) == VisitorStop) {
        return VisitorStop;
      }
      break;

    case AST_STMT_TYPE_STORE:
      if (visitor_visit_expr(visitor, ast->store.lhs) == VisitorStop) {
        return VisitorStop;
      }
      if (visitor_visit_expr(visitor, ast->store.rhs) == VisitorStop) {
        return VisitorStop;
      }
      break;

    case AST_STMT_TYPE_RETURN:
      if (ast->expr && visitor_visit_expr(visitor, ast->expr) == VisitorStop) {
        return VisitorStop;
      }
      break;

    case AST_STMT_TYPE_DEFER:
      if (visitor_visit_expr(visitor, ast->expr) == VisitorStop) {
        return VisitorStop;
      }
      break;

    case AST_STMT_TYPE_WHILE:
      if (visitor_visit_expr(visitor, ast->while_stmt.cond) == VisitorStop) {
        return VisitorStop;
      }
      if (visitor_visit_block(visitor, &ast->while_stmt.block) == VisitorStop) {
        return VisitorStop;
      }
      break;

    case AST_STMT_TYPE_BREAK:
    case AST_STMT_TYPE_CONTINUE:
      break;

    default:
      compiler_log(visitor->compiler, LogLevelError, "ast_visitor", "unhandled statement type %d",
                   ast->type);
  }

  return VisitorContinue;
}

static enum ast_ty_id expr_ty(struct ast_expr *ast) {
  return ast->ty ? ast->ty->ty : ast->parsed_ty.ty;
}

static enum VisitorResult visitor_visit_expr(struct ast_visitor *visitor, struct ast_expr *ast) {
  compiler_log(visitor->compiler, LogLevelTrace, "ast_visitor", "visiting expression [depth=%zd]",
               visitor->depth);

  struct ast_visitor_node node = {.expr = ast, .depth = visitor->depth++};
  enum VisitorResult result = visitor->visit(&node, visitor->user_data);
  --visitor->depth;
  if (result == VisitorStop || result == VisitorSkipChildren) {
    return result;
  }

  switch (ast->type) {
    case AST_EXPR_TYPE_CONSTANT: {
      if (expr_ty(ast) == AST_TYPE_FVEC || expr_ty(ast) == AST_TYPE_ARRAY ||
          expr_ty(ast) == AST_TYPE_MATRIX) {
        if (ast->list) {
          struct ast_expr_list *list_node = ast->list;
          while (list_node) {
            if (visitor_visit_expr(visitor, list_node->expr) == VisitorStop) {
              return VisitorStop;
            }
            list_node = list_node->next;
          }
        }
      }
      return VisitorContinue;
    } break;

    case AST_EXPR_TYPE_BLOCK:
      return visitor_visit_block(visitor, &ast->block);

    case AST_EXPR_TYPE_BINARY:
      if (visitor_visit_expr(visitor, ast->binary.lhs) == VisitorStop) {
        return VisitorStop;
      }
      if (visitor_visit_expr(visitor, ast->binary.rhs) == VisitorStop) {
        return VisitorStop;
      }
      break;

    case AST_EXPR_TYPE_VARIABLE:
      return VisitorContinue;

    case AST_EXPR_TYPE_DEREF:
      if (visitor_visit_expr(visitor, ast->deref.target) == VisitorStop) {
        return VisitorStop;
      }
      break;

    case AST_EXPR_TYPE_VOID:
      break;

    case AST_EXPR_TYPE_CALL: {
      struct ast_expr_list *args = ast->call.args;
      while (args) {
        if (visitor_visit_expr(visitor, args->expr) == VisitorStop) {
          return VisitorStop;
        }
        args = args->next;
      }
    } break;

    case AST_EXPR_TYPE_CAST:
      if (visitor_visit_expr(visitor, ast->cast.expr) == VisitorStop) {
        return VisitorStop;
      }
      break;

    case AST_EXPR_TYPE_UNARY:
      if (visitor_visit_expr(visitor, ast->unary.expr) == VisitorStop) {
        return VisitorStop;
      }
      break;

    case AST_EXPR_TYPE_IF:
      if (visitor_visit_expr(visitor, ast->if_expr.cond) == VisitorStop) {
        return VisitorStop;
      }
      if (visitor_visit_block(visitor, &ast->if_expr.then_block) == VisitorStop) {
        return VisitorStop;
      }
      if (ast->if_expr.elseifs) {
        struct ast_expr_elseif *elseif = ast->if_expr.elseifs;
        while (elseif) {
          if (visitor_visit_expr(visitor, elseif->cond) == VisitorStop) {
            return VisitorStop;
          }
          if (visitor_visit_block(visitor, &elseif->block) == VisitorStop) {
            return VisitorStop;
          }
          elseif = elseif->next;
        }
      }
      if (ast->if_expr.has_else) {
        if (visitor_visit_block(visitor, &ast->if_expr.else_block) == VisitorStop) {
          return VisitorStop;
        }
      }
      break;

    case AST_EXPR_TYPE_ASSIGN:
      if (visitor_visit_expr(visitor, ast->assign.lhs) == VisitorStop) {
        return VisitorStop;
      }
      if (visitor_visit_expr(visitor, ast->assign.expr) == VisitorStop) {
        return VisitorStop;
      }
      break;

    case AST_EXPR_TYPE_REF:
      if (visitor_visit_expr(visitor, ast->ref.expr) == VisitorStop) {
        return VisitorStop;
      }
      break;

    case AST_EXPR_TYPE_LOAD:
      if (visitor_visit_expr(visitor, ast->load.expr) == VisitorStop) {
        return VisitorStop;
      }
      break;

    case AST_EXPR_TYPE_ARRAY_INDEX:
      if (visitor_visit_expr(visitor, ast->array_index.target) == VisitorStop) {
        return VisitorStop;
      }
      if (visitor_visit_expr(visitor, ast->array_index.index) == VisitorStop) {
        return VisitorStop;
      }
      break;

    case AST_EXPR_TYPE_MATCH:
      if (visitor_visit_expr(visitor, ast->match.expr) == VisitorStop) {
        return VisitorStop;
      }
      struct ast_expr_match_arm *arm = ast->match.arms;
      while (arm) {
        if (visitor_visit_expr(visitor, arm->pattern) == VisitorStop) {
          return VisitorStop;
        }
        if (visitor_visit_expr(visitor, arm->expr) == VisitorStop) {
          return VisitorStop;
        }
        arm = arm->next;
      }
      if (ast->match.otherwise) {
        if (visitor_visit_expr(visitor, ast->match.otherwise->expr) == VisitorStop) {
          return VisitorStop;
        }
      }
      break;

    case AST_EXPR_TYPE_STRUCT_INIT: {
      if (ast->list) {
        struct ast_expr_list *list_node = ast->list;
        while (list_node) {
          if (visitor_visit_expr(visitor, list_node->expr) == VisitorStop) {
            return VisitorStop;
          }
          list_node = list_node->next;
        }
      }
    } break;

    case AST_EXPR_TYPE_NIL:
      return VisitorContinue;

    case AST_EXPR_TYPE_PATTERN_MATCH:
      return VisitorContinue;

    case AST_EXPR_TYPE_ENUM_INIT:
      if (ast->enum_init.inner) {
        return visitor_visit_expr(visitor, ast->enum_init.inner);
      }
      break;

    case AST_EXPR_TYPE_UNION_INIT:
      if (visitor_visit_expr(visitor, ast->union_init.inner) == VisitorStop) {
        return VisitorStop;
      }
      break;

    case AST_EXPR_TYPE_SIZEOF:
      if (ast->sizeof_expr.expr &&
          visitor_visit_expr(visitor, ast->sizeof_expr.expr) == VisitorStop) {
        return VisitorStop;
      }
      break;

    case AST_EXPR_TYPE_BOX:
      if (ast->box_expr.expr && visitor_visit_expr(visitor, ast->box_expr.expr) == VisitorStop) {
        return VisitorStop;
      }
      break;

    case AST_EXPR_TYPE_UNBOX:
      if (visitor_visit_expr(visitor, ast->box_expr.expr) == VisitorStop) {
        return VisitorStop;
      }
      break;

    default:
      compiler_log(visitor->compiler, LogLevelError, "ast_visitor", "unhandled expression type %d",
                   ast->type);
  }

  return VisitorContinue;
}
