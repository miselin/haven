#include "typecheck.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "compiler.h"
#include "internal.h"
#include "kv.h"
#include "scope.h"
#include "types.h"

int typecheck_implicit_ast(struct ast_program *ast);

static void typecheck_ast(struct typecheck *typecheck, struct ast_program *ast);
static void typecheck_toplevel(struct typecheck *typecheck, struct ast_toplevel *ast);
static struct ast_ty *typecheck_stmt(struct typecheck *typecheck, struct ast_stmt *ast);

struct typecheck *new_typecheck(struct ast_program *ast, struct compiler *compiler) {
  struct typecheck *result = calloc(1, sizeof(struct typecheck));
  result->ast = ast;
  result->scope = enter_scope(NULL);
  result->aliases = new_kv();
  result->error_type = type_error();
  result->void_type = type_void();
  result->tbd_type = type_tbd();
  result->compiler = compiler;
  result->type_repo = compiler_get_type_repository(compiler);
  return result;
}

int typecheck_run(struct typecheck *typecheck) {
  typecheck_ast(typecheck, typecheck->ast);
  if (typecheck->errors) {
    return typecheck->errors;
  }

  compiler_log(typecheck->compiler, LogLevelInfo, "typecheck",
               "primary typecheck pass complete, moving on to implicit conversions");

  int rc = 0;
  while (1) {
    rc = typecheck_implicit_ast(typecheck->ast);
    if (rc < 0) {
      return 1;
    }

    if (rc == 0) {
      break;
    }
  }

  compiler_log(typecheck->compiler, LogLevelInfo, "typecheck",
               "implicit conversion pass complete, moving on to verification");

  if (typecheck_verify_ast(typecheck->compiler, typecheck->ast) < 0) {
    return 1;
  }
  return 0;
}

void destroy_typecheck(struct typecheck *typecheck) {
  exit_scope(typecheck->scope);
  void *iter = kv_iter(typecheck->aliases);
  while (!kv_end(iter)) {
    struct alias_entry *entry = kv_next(&iter);
    free(entry);
  }
  destroy_kv(typecheck->aliases);
  free(typecheck);
}

static void typecheck_ast(struct typecheck *typecheck, struct ast_program *ast) {
  struct ast_toplevel *decl = ast->decls;
  while (decl) {
    typecheck_toplevel(typecheck, decl);
    decl = decl->next;
  }
}

static void typecheck_toplevel(struct typecheck *typecheck, struct ast_toplevel *ast) {
  compiler_log(typecheck->compiler, LogLevelDebug, "typecheck", "checking toplevel %d @ %s:%zd:%zd",
               ast->type, ast->loc.file, ast->loc.line, ast->loc.column);

  if (ast->type == AST_DECL_TYPE_TYDECL) {
    if (ast->tydecl.parsed_ty.ty == AST_TYPE_CUSTOM) {
      // trying to make an alias to another existing type
      struct ast_ty *target_ty =
          type_repository_lookup(typecheck->type_repo, ast->tydecl.parsed_ty.name);
      if (!target_ty) {
        fprintf(stderr, "type %s not found [%s]\n", ast->tydecl.parsed_ty.name,
                ast->tydecl.ident.value.identv.ident);
        ++typecheck->errors;
        return;
      }

      // already registered?
      struct ast_ty *registered =
          type_repository_lookup(typecheck->type_repo, ast->tydecl.ident.value.identv.ident);
      if (!registered) {
        if (!type_repository_register_alias(typecheck->type_repo,
                                            ast->tydecl.ident.value.identv.ident, target_ty)) {
          compiler_log(typecheck->compiler, LogLevelError, "typecheck",
                       "failed to register alias %s -> %s", ast->tydecl.ident.value.identv.ident,
                       ast->tydecl.parsed_ty.name);
          ++typecheck->errors;
        }

        registered = target_ty;
      }

      ast->tydecl.resolved = registered;
      return;
    }

    const char *alias_name = ast->tydecl.ident.value.identv.ident;

    // not custom - defining a real type

    strncpy(ast->tydecl.parsed_ty.name, alias_name, 256);

    // already resolved? don't do additional work
    struct ast_ty *existing = type_repository_lookup(typecheck->type_repo, alias_name);
    if (existing) {
      // TODO: potentially update it to be a new type?
      ast->tydecl.resolved = existing;
      return;
    }

    // set initial lookup for this name to allow for recursive types in structs and unions
    // struct ast_ty *tbd = type_repository_tbd(typecheck->type_repo);
    struct ast_ty custom;
    memset(&custom, 0, sizeof(struct ast_ty));
    custom.ty = AST_TYPE_CUSTOM;
    strncpy(custom.name, alias_name, 256);
    compiler_log(typecheck->compiler, LogLevelDebug, "typecheck",
                 "registering alias %s -> custom %s", alias_name, custom.name);
    type_repository_register_alias(typecheck->type_repo, alias_name, &custom);
    compiler_log(typecheck->compiler, LogLevelDebug, "typecheck",
                 "moving on with type alias creation");

    struct ast_ty *target_type = resolve_parsed_type(typecheck, &ast->tydecl.parsed_ty);

    compiler_log(typecheck->compiler, LogLevelTrace, "typecheck", "registering tydecl %s -> %s",
                 alias_name, target_type->name);

    type_repository_overwrite_alias(typecheck->type_repo, alias_name, target_type);

    compiler_log(typecheck->compiler, LogLevelTrace, "typecheck", "patching tydecl %s -> %s",
                 alias_name, target_type->name);

    patch_type_tbds(typecheck, target_type, &ast->tydecl.parsed_ty);

    compiler_log(typecheck->compiler, LogLevelTrace, "typecheck", "resolved tydecl %s -> %s",
                 alias_name, target_type->name);
    ast->tydecl.resolved = target_type;
  }

  if (ast->type == AST_DECL_TYPE_FDECL) {
    ast->fdecl.retty = resolve_parsed_type(typecheck, &ast->fdecl.parsed_retty);
    if (!ast->fdecl.retty || type_is_error(ast->fdecl.retty) || type_is_tbd(ast->fdecl.retty)) {
      fprintf(stderr, "function %s has unresolved return type\n",
              ast->fdecl.ident.value.identv.ident);
      ++typecheck->errors;
      return;
    }

    // specialize the return type if it's a template
    if (ast->fdecl.retty->ty == AST_TYPE_ENUM && ast->fdecl.retty->enumty.templates) {
      // create a specialized type and use it here
      struct ast_ty new_type;
      memset(&new_type, 0, sizeof(struct ast_ty));
      new_type.ty = AST_TYPE_ENUM;
      char new_name[1024];
      if (snprintf(new_name, 1024, "%s_spec_%s", ast->fdecl.ident.value.identv.ident,
                   ast->fdecl.retty->name) > 256) {
        fprintf(stderr, "enum specialization name too long\n");
        ++typecheck->errors;
        return;
      }
      strcpy(new_type.name, new_name);

      new_type.enumty.fields = ast->fdecl.retty->enumty.fields;
      new_type.enumty.no_wrapped_fields = ast->fdecl.retty->enumty.no_wrapped_fields;
      new_type.enumty.num_fields = ast->fdecl.retty->enumty.num_fields;
      new_type.enumty.templates = ast->fdecl.retty->enumty.templates;

      new_type.specialization_of = strdup(ast->fdecl.retty->name);
    }

    struct scope_entry *existing =
        scope_lookup(typecheck->scope, ast->fdecl.ident.value.identv.ident, 1);

    struct scope_entry *entry = calloc(1, sizeof(struct scope_entry));
    entry->fdecl = &ast->fdecl;

    scope_insert(typecheck->scope, ast->fdecl.ident.value.identv.ident, entry);

    if (existing && entry->fdecl->flags != existing->fdecl->flags) {
      fprintf(stderr, "function %s redeclared with different flags\n",
              ast->fdecl.ident.value.identv.ident);
      ++typecheck->errors;
    }

    if (existing && !same_type(entry->fdecl->retty, existing->fdecl->retty)) {
      char tystr[256], existingstr[256];
      type_name_into(entry->fdecl->retty, tystr, 256);
      type_name_into(existing->fdecl->retty, existingstr, 256);

      fprintf(stderr, "function %s redeclared with different return type %s, expected %s\n",
              ast->fdecl.ident.value.identv.ident, tystr, existingstr);
      ++typecheck->errors;
    }

    if (existing && entry->fdecl->num_params != existing->fdecl->num_params) {
      fprintf(stderr,
              "function %s redeclared with different number of parameters %zu, expected %zu\n",
              ast->fdecl.ident.value.identv.ident, entry->fdecl->num_params,
              existing->fdecl->num_params);
      ++typecheck->errors;
    }

    for (size_t i = 0; i < entry->fdecl->num_params; ++i) {
      entry->fdecl->params[i]->ty =
          resolve_parsed_type(typecheck, &entry->fdecl->params[i]->parser_ty);

      if (existing && !same_type(entry->fdecl->params[i]->ty, existing->fdecl->params[i]->ty)) {
        char tystr[256], existingstr[256];
        type_name_into(entry->fdecl->params[i]->ty, tystr, 256);
        type_name_into(existing->fdecl->params[i]->ty, existingstr, 256);

        fprintf(stderr, "function %s parameter %zu has type %s, expected %s\n",
                ast->fdecl.ident.value.identv.ident, i, tystr, existingstr);
        ++typecheck->errors;
      }
    }

    // done with the old entry, new definition is compatible
    if (existing) {
      free(existing);
    }

    if (ast->fdecl.body) {
      typecheck->scope = enter_scope(typecheck->scope);

      // declare the parameters in the function scope
      for (size_t i = 0; i < ast->fdecl.num_params; i++) {
        struct scope_entry *param_entry = calloc(1, sizeof(struct scope_entry));
        param_entry->vdecl = ast->fdecl.params[i];
        scope_insert(typecheck->scope, ast->fdecl.params[i]->ident.value.identv.ident, param_entry);
      }

      {
        struct ast_ty *result = typecheck_block(typecheck, ast->fdecl.body);
        if (!result) {
          return;
        }
      }

      typecheck->scope = exit_scope(typecheck->scope);

      maybe_implicitly_convert(&ast->fdecl.body->ty, &ast->fdecl.retty);

      if (!same_type(ast->fdecl.body->ty, ast->fdecl.retty)) {
        char resultstr[256], tystr[256];
        type_name_into(ast->fdecl.body->ty, resultstr, 256);
        type_name_into(ast->fdecl.retty, tystr, 256);

        fprintf(stderr, "function %s returns %s, expected %s\n",
                ast->fdecl.ident.value.identv.ident, resultstr, tystr);
        ++typecheck->errors;
        return;
      }
    }
  }

  if (ast->type == AST_DECL_TYPE_VDECL) {
    ast->vdecl.ty = resolve_parsed_type(typecheck, &ast->vdecl.parser_ty);
    if (!ast->vdecl.ty || type_is_error(ast->vdecl.ty) || type_is_tbd(ast->vdecl.ty)) {
      fprintf(stderr, "variable %s has unresolved type\n", ast->vdecl.ident.value.identv.ident);
      ++typecheck->errors;
      return;
    }

    struct scope_entry *existing =
        scope_lookup(typecheck->scope, ast->vdecl.ident.value.identv.ident, 0);
    if (existing) {
      fprintf(stderr, "typecheck: multiple definitions of variable %s\n",
              ast->vdecl.ident.value.identv.ident);
      ++typecheck->errors;
      return;
    }

    struct scope_entry *entry = calloc(1, sizeof(struct scope_entry));
    entry->vdecl = &ast->vdecl;

    // insert before checking the initializer to allow recursive references
    scope_insert(typecheck->scope, ast->vdecl.ident.value.identv.ident, entry);

    if (ast->vdecl.init_expr) {
      {
        struct ast_ty *result = typecheck_expr(typecheck, ast->vdecl.init_expr);
        if (!result) {
          return;
        }
      }

      maybe_implicitly_convert(&ast->vdecl.init_expr->ty, &ast->vdecl.ty);

      if (!same_type(ast->vdecl.init_expr->ty, ast->vdecl.ty)) {
        char resultstr[256], tystr[256];
        type_name_into(ast->vdecl.init_expr->ty, resultstr, 256);
        type_name_into(ast->vdecl.ty, tystr, 256);

        typecheck_diag_expr(typecheck, ast->vdecl.init_expr,
                            "variable %s initializer has type %s, expected %s\n",
                            ast->vdecl.ident.value.identv.ident, resultstr, tystr);
        ++typecheck->errors;
        return;
      }
    }
  }

  if (ast->type == AST_DECL_TYPE_IMPORT) {
    typecheck_ast(typecheck, ast->import.ast);
  }
}

struct ast_ty *typecheck_block(struct typecheck *typecheck, struct ast_block *ast) {
  typecheck->scope = enter_scope(typecheck->scope);

  struct ast_stmt *stmt = ast->stmt;
  struct ast_ty *last_ty = NULL;

  while (stmt) {
    struct ast_ty *ty = typecheck_stmt(typecheck, stmt);
    if (!ty) {
      return NULL;
    }

    last_ty = ty;
    stmt = stmt->next;
  }

  if (!last_ty) {
    last_ty = &typecheck->void_type;
  }

  typecheck->scope = exit_scope(typecheck->scope);

  ast->ty = last_ty;
  return ast->ty;
}

static struct ast_ty *typecheck_stmt(struct typecheck *typecheck, struct ast_stmt *ast) {
  switch (ast->type) {
    case AST_STMT_TYPE_EXPR:
      return typecheck_expr(typecheck, ast->expr);

    case AST_STMT_TYPE_LET: {
      struct scope_entry *entry = calloc(1, sizeof(struct scope_entry));
      entry->vdecl = &ast->let;

      // if a type was actually specified we need to resolve it
      if (!type_is_tbd(&ast->let.parser_ty)) {
        ast->let.ty = resolve_parsed_type(typecheck, &ast->let.parser_ty);
      } else {
        ast->let.ty = type_repository_tbd(typecheck->type_repo);
      }

      // insert before checking the initializer to allow recursive references
      scope_insert(typecheck->scope, ast->let.ident.value.identv.ident, entry);
      {
        struct ast_ty *init_ty = typecheck_expr(typecheck, ast->let.init_expr);
        if (!init_ty) {
          return NULL;
        }

        if (type_is_tbd(ast->let.ty)) {
          if (init_ty->flags & TYPE_FLAG_CONSTANT) {
            struct ast_ty new_ty = *init_ty;
            new_ty.flags &= ~TYPE_FLAG_CONSTANT;
            init_ty = type_repository_lookup_ty(typecheck->type_repo, &new_ty);
            if (!init_ty) {
              init_ty = type_repository_register(typecheck->type_repo, &new_ty);
            }
          }

          // inferred type
          ast->let.ty = init_ty;
        }
      }

      maybe_implicitly_convert(&ast->let.init_expr->ty, &ast->let.ty);

      if (!same_type(ast->let.ty, ast->let.init_expr->ty)) {
        char tystr[256], initstr[256];
        type_name_into(ast->let.ty, tystr, 256);
        type_name_into(ast->let.init_expr->ty, initstr, 256);

        typecheck_diag_expr(typecheck, ast->let.init_expr,
                            "let %s initializer has type %s, expected %s\n",
                            ast->let.ident.value.identv.ident, initstr, tystr);
        ++typecheck->errors;
      }
    } break;

    case AST_STMT_TYPE_ITER: {
      struct ast_ty *start = typecheck_expr(typecheck, ast->iter.range.start);
      struct ast_ty *end = typecheck_expr(typecheck, ast->iter.range.end);
      struct ast_ty *step, step_ty;
      if (ast->iter.range.step) {
        step = typecheck_expr(typecheck, ast->iter.range.step);
      } else {
        step_ty.ty = AST_TYPE_INTEGER;
        step_ty.integer.is_signed = 1;
        step_ty.integer.width = 32;
        step = type_repository_lookup_ty(typecheck->type_repo, &step_ty);
      }

      if (!same_type(start, end)) {
        char startstr[256], endstr[256];
        type_name_into(start, startstr, 256);
        type_name_into(end, endstr, 256);

        fprintf(stderr, "iter range start has type %s, end has type %s\n", startstr, endstr);
        ++typecheck->errors;
      }

      if (ast->iter.range.step && !same_type(start, step)) {
        char startstr[256], stepstr[256];
        type_name_into(start, startstr, 256);
        type_name_into(step, stepstr, 256);

        fprintf(stderr, "iter range start has type %s, step has type %s\n", startstr, stepstr);
        ++typecheck->errors;
      }

      struct ast_vdecl *index = calloc(1, sizeof(struct ast_vdecl));

      struct scope_entry *entry = calloc(1, sizeof(struct scope_entry));
      entry->vdecl = index;
      entry->vdecl->ident = ast->iter.index.ident;
      entry->vdecl->ty = ast->iter.range.start->ty;
      ast->iter.index_vdecl = entry->vdecl;

      // new scope for the loop variable
      typecheck->scope = enter_scope(typecheck->scope);

      // insert before checking the initializer to allow recursive references
      scope_insert(typecheck->scope, ast->iter.index.ident.value.identv.ident, entry);

      if (!typecheck_block(typecheck, &ast->iter.block)) {
        return NULL;
      }

      typecheck->scope = exit_scope(typecheck->scope);
    } break;

    case AST_STMT_TYPE_STORE: {
      struct ast_ty *lhs = typecheck_expr(typecheck, ast->store.lhs);
      struct ast_ty *rhs = typecheck_expr(typecheck, ast->store.rhs);

      if (!lhs || !rhs) {
        return NULL;
      }

      if (lhs->ty != AST_TYPE_POINTER) {
        fprintf(stderr, "store lhs is not a pointer\n");
        ++typecheck->errors;
      }

      struct ast_ty *pointee = ptr_pointee_type(lhs);

      if (!same_type(pointee, rhs)) {
        char lhsstr[256], rhsstr[256];
        type_name_into(pointee, lhsstr, 256);
        type_name_into(rhs, rhsstr, 256);

        fprintf(stderr, "store lhs has type %s, rhs has type %s\n", lhsstr, rhsstr);
        ++typecheck->errors;
      }

    } break;

    case AST_STMT_TYPE_RETURN: {
      // TODO: make sure this expr type matches the function's return type
      if (ast->expr) {
        return typecheck_expr(typecheck, ast->expr);
      } else {
        return &typecheck->void_type;
      }
    } break;

    case AST_STMT_TYPE_DEFER: {
      if (!typecheck_expr(typecheck, ast->expr)) {
        return NULL;
      }
      // expression type is irrelevant; defer is a void statement
    } break;

    case AST_STMT_TYPE_WHILE: {
      struct ast_ty *cond = typecheck_expr(typecheck, ast->while_stmt.cond);
      if (!cond) {
        return NULL;
      }

      // TODO: needs to be an integer condition

      if (!typecheck_block(typecheck, &ast->while_stmt.block)) {
        return NULL;
      }
    } break;

    case AST_STMT_TYPE_BREAK:
    case AST_STMT_TYPE_CONTINUE:
      // no types to check
      break;

    default:
      typecheck_diag_expr(typecheck, NULL, "typecheck: unhandled statement type %d\n", ast->type);
  }

  // statements that aren't expressions do not have types (their expressions do)
  return &typecheck->void_type;
}

struct ast_ty *typecheck_pattern_match(struct typecheck *typecheck, struct ast_expr *ast,
                                       struct ast_expr_pattern_match *pattern,
                                       struct ast_ty *match_ty) {
  if (ast->type != AST_EXPR_TYPE_PATTERN_MATCH) {
    return typecheck_expr(typecheck, ast);
  }

  if (match_ty->ty != AST_TYPE_ENUM) {
    typecheck_diag_expr(typecheck, ast, "match type is not an enum\n");
    return &typecheck->error_type;
  }

  struct ast_enum_field *field = match_ty->enumty.fields;
  while (field) {
    if (!strcmp(field->name, pattern->name.value.identv.ident)) {
      break;
    }
    field = field->next;
  }

  if (!field) {
    typecheck_diag_expr(typecheck, ast, "enum field %s not found in enum %s\n",
                        pattern->name.value.identv.ident, pattern->enum_name.value.identv.ident);
    return &typecheck->error_type;
  }

  if (pattern->inner_vdecl) {
    pattern->inner_vdecl->ty = resolve_parsed_type(typecheck, &field->parser_inner);
  }

  // no need to check inner, it'll become the type of the pattern match in the handler for the
  // match expression
  ast->ty = resolve_type(typecheck, match_ty);
  return ast->ty;
}

void typecheck_diag_expr(struct typecheck *typecheck, struct ast_expr *expr, const char *msg, ...) {
  char msgbuf[1024];

  va_list args;
  va_start(args, msg);
  vsprintf(msgbuf, msg, args);
  va_end(args);

  compiler_diag(typecheck->compiler, DiagError, "%s:%zu:%zu: %s", expr->loc.file, expr->loc.line,
                expr->loc.column, msgbuf);

  ++typecheck->errors;
}
