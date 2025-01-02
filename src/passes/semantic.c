#include "semantic.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "compiler.h"
#include "kv.h"
#include "lex.h"
#include "scope.h"
#include "typecheck.h"
#include "types.h"
#include "utility.h"

struct semantic {
  struct ast_program *ast;
  struct compiler *compiler;
  int pass;

  size_t loop_depth;
};

int check_semantic_ast(struct semantic *semantic, struct ast_program *ast);
static int check_semantic_toplevel(struct semantic *semantic, struct ast_toplevel *ast);
static int check_semantic_block(struct semantic *semantic, struct ast_block *ast);
static int check_semantic_stmt(struct semantic *semantic, struct ast_stmt *ast);
static int check_semantic_expr(struct semantic *semantic, struct ast_expr *ast);
static int check_semantic_tydecl(struct semantic *semantic, struct ast_tydecl *ast);

__attribute__((format(printf, 4, 5))) static void semantic_diag_at(struct semantic *semantic,
                                                                   enum DiagLevel level,
                                                                   struct lex_locator *loc,
                                                                   const char *msg, ...) {
  char msgbuf[1024];

  va_list args;
  va_start(args, msg);
  vsnprintf(msgbuf, 1024, msg, args);
  va_end(args);

  if (loc) {
    compiler_diag(semantic->compiler, level, "semantic error at %s:%zu:%zu: %s\n", loc->file,
                  loc->line, loc->column, msgbuf);
  } else {
    compiler_diag(semantic->compiler, level, "semantic error: %s\n", msgbuf);
  }
}

struct semantic *semantic_new(struct ast_program *ast, struct compiler *compiler, int pass) {
  struct semantic *semantic = calloc(1, sizeof(struct semantic));
  semantic->ast = ast;
  semantic->compiler = compiler;
  semantic->pass = pass;
  return semantic;
}

int semantic_run(struct semantic *semantic) {
  return check_semantic_ast(semantic, semantic->ast);
}

void semantic_destroy(struct semantic *semantic) {
  free(semantic);
}

int check_semantic_ast(struct semantic *semantic, struct ast_program *ast) {
  struct ast_toplevel *decl = ast->decls;
  while (decl) {
    if (check_semantic_toplevel(semantic, decl) < 0) {
      return -1;
    }
    decl = decl->next;
  }

  return 0;
}

static int check_semantic_toplevel(struct semantic *semantic, struct ast_toplevel *ast) {
  if (ast->type == AST_DECL_TYPE_FDECL) {
    if (ast->toplevel.fdecl.body) {
      if (check_semantic_block(semantic, ast->toplevel.fdecl.body) < 0) {
        return -1;
      }
    }
  } else if (ast->type == AST_DECL_TYPE_VDECL) {
    if (ast->toplevel.vdecl.init_expr) {
      if (check_semantic_expr(semantic, ast->toplevel.vdecl.init_expr) < 0) {
        return -1;
      }
    }
  } else if (ast->type == AST_DECL_TYPE_TYDECL) {
    if (check_semantic_tydecl(semantic, &ast->toplevel.tydecl) < 0) {
      return -1;
    }
  } else if (ast->type == AST_DECL_TYPE_IMPORT) {
    if (check_semantic_ast(semantic, ast->toplevel.import.ast) < 0) {
      return -1;
    }
  }

  return 0;
}

static int check_semantic_block(struct semantic *semantic, struct ast_block *ast) {
  struct ast_stmt *stmt = ast->stmt;
  while (stmt) {
    if (check_semantic_stmt(semantic, stmt) < 0) {
      return -1;
    }
    stmt = stmt->next;
  }

  return 0;
}

static int check_semantic_stmt(struct semantic *semantic, struct ast_stmt *ast) {
  switch (ast->type) {
    case AST_STMT_TYPE_EXPR:
      return check_semantic_expr(semantic, ast->stmt.expr);

    case AST_STMT_TYPE_LET: {
      return check_semantic_expr(semantic, ast->stmt.let.init_expr);
    } break;

    case AST_STMT_TYPE_ITER: {
      if (!(ast->stmt.iter.range.start && ast->stmt.iter.range.end)) {
        semantic_diag_at(semantic, DiagError, &ast->loc, "iteration range must have start and end");
        return -1;
      }

      if (check_semantic_expr(semantic, ast->stmt.iter.range.start) < 0) {
        return -1;
      }

      if (check_semantic_expr(semantic, ast->stmt.iter.range.end) < 0) {
        return -1;
      }

      if (ast->stmt.iter.range.step) {
        if (check_semantic_expr(semantic, ast->stmt.iter.range.step) < 0) {
          return -1;
        }

        if (semantic->pass > 0) {
          struct ast_expr *step = ast->stmt.iter.range.step;
          if (step->type == AST_EXPR_TYPE_CAST) {
            step = step->expr.cast.expr;
          }

          if (step->type != AST_EXPR_TYPE_CONSTANT) {
            semantic_diag_at(semantic, DiagError, &ast->loc, "step must be a constant expression");
            return -1;
          }
        }
      }

      if (semantic->pass > 0) {
        int direction = 1;
        struct ast_expr *step = ast->stmt.iter.range.step;
        if (step) {
          if (step->type == AST_EXPR_TYPE_CAST) {
            step = step->expr.cast.expr;
          }

          if ((int64_t)step->expr.constant.constant.value.intv.val < 0) {
            direction = -1;
          }
        }

        struct ast_expr *start_expr = ast->stmt.iter.range.start;
        struct ast_expr *end_expr = ast->stmt.iter.range.end;
        if (start_expr->type == AST_EXPR_TYPE_CAST) {
          start_expr = start_expr->expr.cast.expr;
        }
        if (end_expr->type == AST_EXPR_TYPE_CAST) {
          end_expr = end_expr->expr.cast.expr;
        }

        // if the start/end are constants, check that the direction leads to at least one
        // iteration
        if (start_expr->type == AST_EXPR_TYPE_CONSTANT &&
            end_expr->type == AST_EXPR_TYPE_CONSTANT) {
          int64_t start = (int64_t)start_expr->expr.constant.constant.value.intv.val;
          int64_t end = (int64_t)end_expr->expr.constant.constant.value.intv.val;
          if (start == end) {
            semantic_diag_at(semantic, DiagError, &ast->loc, "iteration range is empty");
            return -1;
          }

          if (direction == 1 && start > end) {
            semantic_diag_at(semantic, DiagError, &ast->loc, "iteration range is empty");
            return -1;
          }

          if (direction == -1 && start < end) {
            semantic_diag_at(semantic, DiagError, &ast->loc, "iteration range is empty");
            return -1;
          }
        }
      }

      semantic->loop_depth++;
      int rc = check_semantic_block(semantic, &ast->stmt.iter.block);
      semantic->loop_depth--;
      return rc;
    } break;

    case AST_STMT_TYPE_STORE: {
      if (check_semantic_expr(semantic, ast->stmt.store.lhs) < 0) {
        return -1;
      }
      if (check_semantic_expr(semantic, ast->stmt.store.rhs) < 0) {
        return -1;
      }
    } break;

    case AST_STMT_TYPE_RETURN: {
      if (ast->stmt.expr) {
        return check_semantic_expr(semantic, ast->stmt.expr);
      }
    } break;

    case AST_STMT_TYPE_DEFER: {
      return check_semantic_expr(semantic, ast->stmt.expr);
    } break;

    case AST_STMT_TYPE_WHILE: {
      if (check_semantic_expr(semantic, ast->stmt.while_stmt.cond) < 0) {
        return -1;
      }

      semantic->loop_depth++;
      int rc = check_semantic_block(semantic, &ast->stmt.while_stmt.block);
      semantic->loop_depth--;
      return rc;
    } break;

    case AST_STMT_TYPE_BREAK:
    case AST_STMT_TYPE_CONTINUE:
      if (semantic->loop_depth == 0) {
        semantic_diag_at(semantic, DiagError, &ast->loc, "break/continue outside of loop");
        return -1;
      }
      break;

    default:
      semantic_diag_at(semantic, DiagError, &ast->loc, "unhandled statement type %d", ast->type);
      return -1;
  }

  return 0;
}

static int check_semantic_expr(struct semantic *semantic, struct ast_expr *ast) {
  switch (ast->type) {
    case AST_EXPR_TYPE_CONSTANT: {
      switch (ast->parsed_ty.ty) {
        case AST_TYPE_FVEC:
        case AST_TYPE_ARRAY:
        case AST_TYPE_MATRIX: {
          struct ast_expr_list *node = ast->expr.list;
          while (node) {
            if (check_semantic_expr(semantic, node->expr) < 0) {
              return -1;
            }
            node = node->next;
          }
        } break;
        default:
          break;
      }
    } break;

    case AST_EXPR_TYPE_STRUCT_INIT: {
      struct ast_expr_list *node = ast->expr.list;
      while (node) {
        if (check_semantic_expr(semantic, node->expr) < 0) {
          return -1;
        }

        node = node->next;
      }

      return 0;
    } break;

    case AST_EXPR_TYPE_VARIABLE:
      break;

    case AST_EXPR_TYPE_ARRAY_INDEX:
      break;

    case AST_EXPR_TYPE_BINARY: {
      if (check_semantic_expr(semantic, ast->expr.binary.lhs) < 0) {
        return -1;
      }
      if (check_semantic_expr(semantic, ast->expr.binary.rhs) < 0) {
        return -1;
      }
    } break;

    case AST_EXPR_TYPE_BLOCK: {
      return check_semantic_block(semantic, &ast->expr.block);
    } break;

    case AST_EXPR_TYPE_CALL: {
      struct ast_expr_list *args = ast->expr.call.args;
      while (args) {
        if (check_semantic_expr(semantic, args->expr) < 0) {
          return -1;
        }

        args = args->next;
      }
    } break;

    case AST_EXPR_TYPE_DEREF:
      return check_semantic_expr(semantic, ast->expr.deref.target);
      break;

    case AST_EXPR_TYPE_VOID:
      break;

    case AST_EXPR_TYPE_CAST: {
      return check_semantic_expr(semantic, ast->expr.cast.expr);
    } break;

    case AST_EXPR_TYPE_IF: {
      if (check_semantic_expr(semantic, ast->expr.if_expr.cond) < 0) {
        return -1;
      }

      if (check_semantic_block(semantic, &ast->expr.if_expr.then_block) < 0) {
        return -1;
      }

      if (ast->expr.if_expr.elseifs) {
        struct ast_expr_elseif *elseif = ast->expr.if_expr.elseifs;
        while (elseif) {
          if (check_semantic_expr(semantic, elseif->cond) < 0) {
            return -1;
          }

          if (check_semantic_block(semantic, &elseif->block) < 0) {
            return -1;
          }

          elseif = elseif->next;
        }
      }

      if (check_semantic_block(semantic, &ast->expr.if_expr.else_block) < 0) {
        return -1;
      }
    } break;

    case AST_EXPR_TYPE_ASSIGN: {
      return check_semantic_expr(semantic, ast->expr.assign.expr);
    } break;

    case AST_EXPR_TYPE_REF:
      break;

    case AST_EXPR_TYPE_LOAD: {
      return check_semantic_expr(semantic, ast->expr.load.expr);
    } break;

    case AST_EXPR_TYPE_UNARY: {
      return check_semantic_expr(semantic, ast->expr.unary.expr);
    } break;

    case AST_EXPR_TYPE_MATCH: {
      if (check_semantic_expr(semantic, ast->expr.match.expr) < 0) {
        return -1;
      }

      if (!ast->expr.match.otherwise) {
        semantic_diag_at(semantic, DiagError, &ast->loc,
                         "match expression must have an otherwise arm");
        return -1;
      }

      if (!ast->expr.match.arms) {
        semantic_diag_at(
            semantic, DiagNote, &ast->loc,
            "a match expression should have at least one arm other than the otherwise arm");
        return -1;
      }

      struct ast_expr_match_arm *arm = ast->expr.match.arms;
      while (arm) {
        if (check_semantic_expr(semantic, arm->pattern) < 0) {
          return -1;
        }

        if (check_semantic_expr(semantic, arm->expr) < 0) {
          return -1;
        }

        arm = arm->next;
      }

      if (check_semantic_expr(semantic, ast->expr.match.otherwise->expr) < 0) {
        return -1;
      }
    } break;

    case AST_EXPR_TYPE_NIL:
      break;

    case AST_EXPR_TYPE_ENUM_INIT:
      return ast->expr.enum_init.inner ? check_semantic_expr(semantic, ast->expr.enum_init.inner)
                                       : 0;
      break;

    case AST_EXPR_TYPE_PATTERN_MATCH: {
      // need fully resolved types for this
      if (semantic->pass < 1) {
        return 0;
      }

      struct ast_enum_field *field = ast->ty->enumty.fields;
      while (field) {
        if (!strcmp(field->name, ast->expr.pattern_match.name.value.identv.ident)) {
          break;
        }
        field = field->next;
      }

      if (!field) {
        semantic_diag_at(semantic, DiagError, &ast->loc, "enum field %s not found in enum %s",
                         ast->expr.pattern_match.name.value.identv.ident,
                         ast->expr.pattern_match.enum_name.value.identv.ident);
        return -1;
      }

      if (field->has_inner && !ast->expr.pattern_match.inner_vdecl &&
          !ast->expr.pattern_match.bindings_ignored) {
        semantic_diag_at(semantic, DiagError, &ast->loc,
                         "enum field %s requires a binding in this pattern match, or to explicitly "
                         "opt out of binding with (_)",
                         field->name);
        return -1;
      }
    } break;

    case AST_EXPR_TYPE_UNION_INIT: {
      if (check_semantic_expr(semantic, ast->expr.union_init.inner) < 0) {
        return -1;
      }
    } break;

    case AST_EXPR_TYPE_SIZEOF: {
      if (ast->expr.sizeof_expr.expr) {
        return check_semantic_expr(semantic, ast->expr.sizeof_expr.expr);
      }
    } break;

    case AST_EXPR_TYPE_BOX:
    case AST_EXPR_TYPE_UNBOX:
      if (ast->expr.box_expr.expr) {
        return check_semantic_expr(semantic, ast->expr.box_expr.expr);
      }
      break;

    default:
      semantic_diag_at(semantic, DiagError, &ast->loc, "unhandled expression type %d", ast->type);
      return -1;
  }

  return 0;
}

static int check_semantic_tydecl(struct semantic *semantic, struct ast_tydecl *ast) {
  switch (ast->parsed_ty.ty) {
    case AST_TYPE_ENUM:
      if (!ast->parsed_ty.enumty.fields) {
        semantic_diag_at(semantic, DiagError, NULL, "enum type %s must have at least one field",
                         ast->parsed_ty.name);
        return -1;
      }

      if (ast->parsed_ty.enumty.num_fields >= INT32_MAX) {
        semantic_diag_at(semantic, DiagError, NULL, "enum type %s has too many fields",
                         ast->parsed_ty.name);
        return -1;
      }
      break;

    default:
      break;
  }

  return 0;
}
