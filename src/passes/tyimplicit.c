/**
 * Third typecheck pass - with all types resolved, finds more implicit conversions to make.
 */

#include <ctype.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "kv.h"
#include "scope.h"
#include "typecheck.h"
#include "types.h"
#include "utility.h"

// Returns 1 if an implicit conversion was made. Returns 0 if none were made.
// Run until no more implicit conversions can be made.
int typecheck_implicit_ast(struct ast_program *ast);
static int typecheck_implicit_toplevel(struct ast_toplevel *ast);
static int typecheck_implicit_block(struct ast_block *ast);
static int typecheck_implicit_stmt(struct ast_stmt *ast);
static int typecheck_implicit_expr(struct ast_expr *ast);
static int typecheck_implicit_struct_decl(struct ast_ty *decl);

int maybe_implicitly_convert(struct ast_ty *from, struct ast_ty *to);

static int is_bad_type(struct ast_ty *ty) {
  // custom types should have been resolved by now
  return type_is_error(ty) || type_is_tbd(ty) || ty->ty == AST_TYPE_CUSTOM;
}

int typecheck_implicit_ast(struct ast_program *ast) {
  struct ast_toplevel *decl = ast->decls;
  int total = 0;
  while (decl) {
    int rc = typecheck_implicit_toplevel(decl);
    if (rc < 0) {
      return rc;
    }
    total += rc;
    decl = decl->next;
  }

  return total;
}

static int typecheck_implicit_toplevel(struct ast_toplevel *ast) {
  int total = 0;

  if (ast->type == AST_DECL_TYPE_FDECL) {
    if (ast->fdecl.body) {
      int rc = typecheck_implicit_block(ast->fdecl.body);
      if (rc < 0) {
        return -1;
      }

      total += rc + maybe_implicitly_convert(&ast->fdecl.body->ty, &ast->fdecl.retty);
    }
  } else if (ast->type == AST_DECL_TYPE_VDECL) {
    if (ast->vdecl.init_expr) {
      int rc = typecheck_implicit_expr(ast->vdecl.init_expr);
      if (rc < 0) {
        return -1;
      }
      total += rc;
    }
  } else if (ast->type == AST_DECL_TYPE_TYDECL) {
    if (ast->tydecl.ty.ty == AST_TYPE_STRUCT) {
      return typecheck_implicit_struct_decl(&ast->tydecl.ty);
    }
  }

  return total;
}

static int typecheck_implicit_struct_decl(struct ast_ty *decl) {
  struct ast_struct_field *field = decl->structty.fields;
  while (field) {
    if (is_bad_type(field->ty)) {
      fprintf(stderr, "struct %s field %s has unresolved type\n", decl->name, field->name);
      return -1;
    }

    field = field->next;
  }

  return 0;
}

static int typecheck_implicit_block(struct ast_block *ast) {
  struct ast_stmt *stmt = ast->stmt;
  struct ast_stmt *last_stmt = NULL;
  int total = 0;
  while (stmt) {
    int rc = typecheck_implicit_stmt(stmt);
    if (rc < 0) {
      return -1;
    }
    total += rc;
    last_stmt = stmt;
    stmt = stmt->next;
  }

  if (last_stmt && last_stmt->type == AST_STMT_TYPE_EXPR) {
    total += maybe_implicitly_convert(&last_stmt->expr->ty, &ast->ty);
  }

  return total;
}

static int typecheck_implicit_stmt(struct ast_stmt *ast) {
  switch (ast->type) {
    case AST_STMT_TYPE_EXPR:
      return typecheck_implicit_expr(ast->expr);

    case AST_STMT_TYPE_LET: {
      return typecheck_implicit_expr(ast->let.init_expr);
    } break;

    case AST_STMT_TYPE_ITER: {
      int total = 0;
      int rc = typecheck_implicit_expr(ast->iter.range.start);
      if (rc < 0) {
        return -1;
      }
      total += rc;

      rc = typecheck_implicit_expr(ast->iter.range.end);
      if (rc < 0) {
        return -1;
      }

      if (ast->iter.range.step) {
        rc = typecheck_implicit_expr(ast->iter.range.step);
        if (rc < 0) {
          return -1;
        }

        total += rc;
      }

      return total + typecheck_implicit_block(&ast->iter.block);
    } break;

    case AST_STMT_TYPE_STORE: {
      int a = typecheck_implicit_expr(ast->store.lhs);
      if (a < 0) {
        return -1;
      }
      int b = typecheck_implicit_expr(ast->store.rhs);
      if (b < 0) {
        return -1;
      }
      return a + b;
    } break;

    case AST_STMT_TYPE_RETURN: {
      if (ast->expr) {
        return typecheck_implicit_expr(ast->expr);
      }
    } break;

    case AST_STMT_TYPE_DEFER: {
      return typecheck_implicit_expr(ast->expr);
    } break;

    case AST_STMT_TYPE_WHILE: {
      int rc = typecheck_implicit_expr(ast->while_stmt.cond);
      if (rc < 0) {
        return -1;
      }

      return rc + typecheck_implicit_block(&ast->while_stmt.block);
    } break;

    case AST_STMT_TYPE_BREAK:
    case AST_STMT_TYPE_CONTINUE:
      break;

    default:
      fprintf(stderr, "tyverify: unhandled statement type %d\n", ast->type);
  }

  return 0;
}

static int typecheck_implicit_expr(struct ast_expr *ast) {
  switch (ast->type) {
    case AST_EXPR_TYPE_CONSTANT: {
      switch (ast->ty.ty) {
        case AST_TYPE_FVEC:
        case AST_TYPE_ARRAY: {
          struct ast_expr_list *node = ast->list;
          int total = 0;
          while (node) {
            int rc = typecheck_implicit_expr(node->expr);
            if (rc < 0) {
              return -1;
            }
            total += rc;
            node = node->next;
          }

          return total;
        } break;
        default:
          break;
      }
    } break;

    case AST_EXPR_TYPE_STRUCT_INIT: {
      struct ast_expr_list *node = ast->list;
      int total = 0;
      while (node) {
        int rc = typecheck_implicit_expr(node->expr);
        if (rc < 0) {
          return -1;
        }

        total += rc;

        node = node->next;
      }

      return total;
    } break;

    case AST_EXPR_TYPE_UNION_INIT:
      return typecheck_implicit_expr(ast->union_init.inner);
      break;

    case AST_EXPR_TYPE_VARIABLE:
      break;

    case AST_EXPR_TYPE_ARRAY_INDEX:
      break;

    case AST_EXPR_TYPE_BINARY: {
      int total = maybe_implicitly_convert(&ast->binary.lhs->ty, &ast->ty) +
                  maybe_implicitly_convert(&ast->binary.rhs->ty, &ast->ty);
      int left = typecheck_implicit_expr(ast->binary.lhs);
      int right = typecheck_implicit_expr(ast->binary.rhs);
      if (left < 0 || right < 0) {
        return -1;
      }

      return total + left + right;
    } break;

    case AST_EXPR_TYPE_BLOCK: {
      int rc = typecheck_implicit_block(&ast->block);
      if (rc < 0) {
        return -1;
      }

      return rc + maybe_implicitly_convert(&ast->block.ty, &ast->ty);
    } break;

    case AST_EXPR_TYPE_CALL: {
      struct ast_expr_list *args = ast->call.args;
      int total = 0;
      while (args) {
        int rc = typecheck_implicit_expr(args->expr);
        if (rc < 0) {
          return -1;
        }
        total += rc;

        args = args->next;
      }
      return total;
    } break;

    case AST_EXPR_TYPE_DEREF:
      break;

    case AST_EXPR_TYPE_VOID:
      break;

    case AST_EXPR_TYPE_CAST: {
      int total = maybe_implicitly_convert(&ast->cast.expr->ty, &ast->ty);
      int rc = typecheck_implicit_expr(ast->cast.expr);
      if (rc < 0) {
        return -1;
      }
      return total + rc;
    } break;

    case AST_EXPR_TYPE_IF: {
      int total = 0;
      int rc = typecheck_implicit_expr(ast->if_expr.cond);
      if (rc < 0) {
        return -1;
      }
      total += rc;

      rc = typecheck_implicit_block(&ast->if_expr.then_block);
      if (rc < 0) {
        return -1;
      }
      total += rc;

      total += maybe_implicitly_convert(&ast->if_expr.then_block.ty, &ast->ty);

      if (ast->if_expr.elseifs) {
        struct ast_expr_elseif *elseif = ast->if_expr.elseifs;
        while (elseif) {
          rc = typecheck_implicit_expr(elseif->cond);
          if (rc < 0) {
            return -1;
          }
          total += rc;

          rc = typecheck_implicit_block(&elseif->block);
          if (rc < 0) {
            return -1;
          }
          total += rc;

          total += maybe_implicitly_convert(&elseif->block.ty, &ast->ty);

          elseif = elseif->next;
        }
      }

      rc = typecheck_implicit_block(&ast->if_expr.else_block);
      if (rc < 0) {
        return -1;
      }

      total += maybe_implicitly_convert(&ast->if_expr.else_block.ty, &ast->ty);

      return total + rc;
    } break;

    case AST_EXPR_TYPE_ASSIGN: {
      return typecheck_implicit_expr(ast->assign.expr);
    } break;

    case AST_EXPR_TYPE_REF:
      break;

    case AST_EXPR_TYPE_LOAD: {
      return typecheck_implicit_expr(ast->load.expr);
    } break;

    case AST_EXPR_TYPE_UNARY: {
      int rc = typecheck_implicit_expr(ast->unary.expr);
      if (rc < 0) {
        return rc;
      }

      return maybe_implicitly_convert(&ast->unary.expr->ty, &ast->ty) + rc;
    } break;

    case AST_EXPR_TYPE_MATCH: {
      int rc = typecheck_implicit_expr(ast->match.expr);
      if (rc < 0) {
        return -1;
      }

      int total = rc;

      struct ast_expr_match_arm *arm = ast->match.arms;
      while (arm) {
        // implicit conversion for pattern
        total += maybe_implicitly_convert(&arm->pattern->ty, &ast->match.expr->ty);
        rc = typecheck_implicit_expr(arm->pattern);
        if (rc < 0) {
          return -1;
        }

        total += rc;

        // implicit conversion for expression - try both directions to see if we can widen the type
        total += maybe_implicitly_convert(&arm->expr->ty, &ast->ty);
        // total += maybe_implicitly_convert(&ast->ty, &arm->expr->ty);
        rc = typecheck_implicit_expr(arm->expr);
        if (rc < 0) {
          return -1;
        }

        total += rc;

        arm = arm->next;
      }

      total += maybe_implicitly_convert(&ast->match.otherwise->expr->ty, &ast->ty);
      // total += maybe_implicitly_convert(&ast->ty, &ast->match.otherwise->expr->ty);
      rc = typecheck_implicit_expr(ast->match.otherwise->expr);
      if (rc < 0) {
        return -1;
      }

      return total + rc;
    } break;

    case AST_EXPR_TYPE_NIL:
      break;

    case AST_EXPR_TYPE_PATTERN_MATCH:
      break;

    case AST_EXPR_TYPE_ENUM_INIT:
      if (ast->enum_init.inner) {
        int total = 0;
        int rc = typecheck_implicit_expr(ast->enum_init.inner);
        if (rc < 0) {
          return -1;
        }

        total = rc;

        struct ast_enum_field *field = ast->ty.enumty.fields;
        while (field) {
          if (!strcmp(ast->enum_init.enum_val_name.value.identv.ident, field->name)) {
            break;
          }

          field = field->next;
        }

        if (field) {
          total += maybe_implicitly_convert(&ast->enum_init.inner->ty, &field->inner);
        }

        // if after conversion the result type is a specialization, make sure the enum is set
        // accordingly
        if (ast->ty.specialization_of) {
          strncpy(ast->enum_init.enum_ty_name.value.identv.ident, ast->ty.name, 256);
        }

        return total;
      }
      break;

    case AST_EXPR_TYPE_SIZEOF: {
      if (ast->sizeof_expr.expr) {
        return typecheck_implicit_expr(ast->sizeof_expr.expr);
      }
    } break;

    default:
      fprintf(stderr, "tyimplicit: unhandled expression type %d\n", ast->type);
  }

  return 0;
}
