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
    if (ast->toplevel.tydecl.parsed_ty.ty == AST_TYPE_CUSTOM) {
      // trying to make an alias to another existing type
      struct ast_ty *target_ty =
          type_repository_lookup(typecheck->type_repo, ast->toplevel.tydecl.parsed_ty.name);
      if (!target_ty) {
        fprintf(stderr, "type %s not found [%s]\n", ast->toplevel.tydecl.parsed_ty.name,
                ast->toplevel.tydecl.ident.value.identv.ident);
        ++typecheck->errors;
        return;
      }

      // already registered?
      struct ast_ty *registered = type_repository_lookup(
          typecheck->type_repo, ast->toplevel.tydecl.ident.value.identv.ident);
      if (!registered) {
        if (!type_repository_register_alias(
                typecheck->type_repo, ast->toplevel.tydecl.ident.value.identv.ident, target_ty)) {
          compiler_log(
              typecheck->compiler, LogLevelError, "typecheck", "failed to register alias %s -> %s",
              ast->toplevel.tydecl.ident.value.identv.ident, ast->toplevel.tydecl.parsed_ty.name);
          ++typecheck->errors;
        }

        registered = target_ty;
      }

      ast->toplevel.tydecl.resolved = registered;
      return;
    }

    const char *alias_name = ast->toplevel.tydecl.ident.value.identv.ident;

    // not custom - defining a real type

    strncpy(ast->toplevel.tydecl.parsed_ty.name, alias_name, 256);

    // already resolved? don't do additional work
    struct ast_ty *existing = type_repository_lookup(typecheck->type_repo, alias_name);
    if (existing) {
      // TODO: potentially update it to be a new type?
      ast->toplevel.tydecl.resolved = existing;
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

    struct ast_ty *target_type = resolve_parsed_type(typecheck, &ast->toplevel.tydecl.parsed_ty);

    compiler_log(typecheck->compiler, LogLevelTrace, "typecheck", "registering tydecl %s -> %s",
                 alias_name, target_type->name);

    type_repository_overwrite_alias(typecheck->type_repo, alias_name, target_type);

    compiler_log(typecheck->compiler, LogLevelTrace, "typecheck", "patching tydecl %s -> %s",
                 alias_name, target_type->name);

    patch_type_tbds(typecheck, target_type, &ast->toplevel.tydecl.parsed_ty);

    compiler_log(typecheck->compiler, LogLevelTrace, "typecheck", "resolved tydecl %s -> %s",
                 alias_name, target_type->name);
    ast->toplevel.tydecl.resolved = target_type;
  }

  if (ast->type == AST_DECL_TYPE_FDECL) {
    ast->toplevel.fdecl.function_ty =
        resolve_parsed_type(typecheck, &ast->toplevel.fdecl.parsed_function_ty);
    if (!ast->toplevel.fdecl.function_ty || !ast->toplevel.fdecl.function_ty->function.retty ||
        type_is_error(ast->toplevel.fdecl.function_ty->function.retty) ||
        type_is_tbd(ast->toplevel.fdecl.function_ty->function.retty)) {
      compiler_log(typecheck->compiler, LogLevelError, "typecheck",
                   "function %s has unresolved return type",
                   ast->toplevel.fdecl.ident.value.identv.ident);
      ++typecheck->errors;
      return;
    }

    // specialize the return type if it's a template
    if (ast->toplevel.fdecl.function_ty->function.retty->ty == AST_TYPE_ENUM &&
        ast->toplevel.fdecl.function_ty->function.retty->enumty.templates) {
      // create a specialized type and use it here
      struct ast_ty new_type;
      memset(&new_type, 0, sizeof(struct ast_ty));
      new_type.ty = AST_TYPE_ENUM;
      char new_name[1024];
      if (snprintf(new_name, 1024, "%s_spec_%s", ast->toplevel.fdecl.ident.value.identv.ident,
                   ast->toplevel.fdecl.function_ty->function.retty->name) > 256) {
        fprintf(stderr, "enum specialization name too long\n");
        ++typecheck->errors;
        return;
      }
      strcpy(new_type.name, new_name);

      new_type.enumty.fields = ast->toplevel.fdecl.function_ty->function.retty->enumty.fields;
      new_type.enumty.no_wrapped_fields =
          ast->toplevel.fdecl.function_ty->function.retty->enumty.no_wrapped_fields;
      new_type.enumty.num_fields =
          ast->toplevel.fdecl.function_ty->function.retty->enumty.num_fields;
      new_type.enumty.templates = ast->toplevel.fdecl.function_ty->function.retty->enumty.templates;

      new_type.specialization_of = strdup(ast->toplevel.fdecl.function_ty->function.retty->name);
    }

    struct scope_entry *existing =
        scope_lookup(typecheck->scope, ast->toplevel.fdecl.ident.value.identv.ident, 1);

    struct scope_entry *entry = calloc(1, sizeof(struct scope_entry));
    entry->fdecl = &ast->toplevel.fdecl;
    entry->ty = ast->toplevel.fdecl.function_ty;

    scope_insert(typecheck->scope, ast->toplevel.fdecl.ident.value.identv.ident, entry);

    if (existing && entry->fdecl->flags != existing->fdecl->flags) {
      fprintf(stderr, "function %s redeclared with different flags\n",
              ast->toplevel.fdecl.ident.value.identv.ident);
      ++typecheck->errors;
    }

    if (existing && !same_type(entry->fdecl->function_ty->function.retty,
                               existing->fdecl->function_ty->function.retty)) {
      char tystr[256], existingstr[256];
      type_name_into(entry->fdecl->function_ty->function.retty, tystr, 256);
      type_name_into(existing->fdecl->function_ty->function.retty, existingstr, 256);

      fprintf(stderr, "function %s redeclared with different return type %s, expected %s\n",
              ast->toplevel.fdecl.ident.value.identv.ident, tystr, existingstr);
      ++typecheck->errors;
    }

    if (existing && entry->fdecl->num_params != existing->fdecl->num_params) {
      fprintf(stderr,
              "function %s redeclared with different number of parameters %zu, expected %zu\n",
              ast->toplevel.fdecl.ident.value.identv.ident, entry->fdecl->num_params,
              existing->fdecl->num_params);
      ++typecheck->errors;
    }

    entry->fdecl->function_ty->function.num_params = entry->fdecl->num_params;

    for (size_t i = 0; i < entry->fdecl->num_params; ++i) {
      entry->fdecl->function_ty->function.param_types[i] =
          resolve_parsed_type(typecheck, entry->fdecl->parsed_function_ty.function.param_types[i]);

      if (existing && !same_type(entry->fdecl->function_ty->function.param_types[i],
                                 existing->fdecl->function_ty->function.param_types[i])) {
        char tystr[256], existingstr[256];
        type_name_into(entry->fdecl->function_ty->function.param_types[i], tystr, 256);
        type_name_into(existing->fdecl->function_ty->function.param_types[i], existingstr, 256);

        fprintf(stderr, "function %s parameter %zu has type %s, expected %s\n",
                ast->toplevel.fdecl.ident.value.identv.ident, i, tystr, existingstr);
        ++typecheck->errors;
      }
    }

    // done with the old entry, new definition is compatible
    if (existing) {
      free(existing);
    }

    if (ast->toplevel.fdecl.body) {
      typecheck->scope = enter_scope(typecheck->scope);

      // declare the parameters in the function scope
      for (size_t i = 0; i < ast->toplevel.fdecl.num_params; i++) {
        struct scope_entry *param_entry = calloc(1, sizeof(struct scope_entry));
        param_entry->ty = ast->toplevel.fdecl.function_ty->function.param_types[i];
        param_entry->decl_flags = DECL_FLAG_TEMPORARY | ast->toplevel.fdecl.params[i].flags;
        scope_insert(typecheck->scope, ast->toplevel.fdecl.params[i].name, param_entry);
      }

      {
        struct ast_ty *result = typecheck_block(typecheck, ast->toplevel.fdecl.body);
        if (!result) {
          return;
        }
      }

      typecheck->scope = exit_scope(typecheck->scope);

      maybe_implicitly_convert(&ast->toplevel.fdecl.body->ty,
                               &ast->toplevel.fdecl.function_ty->function.retty);

      if (!same_type(ast->toplevel.fdecl.body->ty,
                     ast->toplevel.fdecl.function_ty->function.retty)) {
        char resultstr[256], tystr[256];
        type_name_into(ast->toplevel.fdecl.body->ty, resultstr, 256);
        type_name_into(ast->toplevel.fdecl.function_ty->function.retty, tystr, 256);

        fprintf(stderr, "function %s returns %s, expected %s\n",
                ast->toplevel.fdecl.ident.value.identv.ident, resultstr, tystr);
        ++typecheck->errors;
        return;
      }
    }
  }

  if (ast->type == AST_DECL_TYPE_VDECL) {
    ast->toplevel.vdecl.ty = resolve_parsed_type(typecheck, &ast->toplevel.vdecl.parser_ty);
    if (!ast->toplevel.vdecl.ty || type_is_error(ast->toplevel.vdecl.ty) ||
        type_is_tbd(ast->toplevel.vdecl.ty)) {
      fprintf(stderr, "variable %s has unresolved type\n",
              ast->toplevel.vdecl.ident.value.identv.ident);
      ++typecheck->errors;
      return;
    }

    struct scope_entry *existing =
        scope_lookup(typecheck->scope, ast->toplevel.vdecl.ident.value.identv.ident, 0);
    if (existing) {
      fprintf(stderr, "typecheck: multiple definitions of variable %s\n",
              ast->toplevel.vdecl.ident.value.identv.ident);
      ++typecheck->errors;
      return;
    }

    struct scope_entry *entry = calloc(1, sizeof(struct scope_entry));
    entry->ty = ast->toplevel.vdecl.ty;
    entry->decl_flags = ast->toplevel.vdecl.flags;

    // insert before checking the initializer to allow recursive references
    scope_insert(typecheck->scope, ast->toplevel.vdecl.ident.value.identv.ident, entry);

    if (ast->toplevel.vdecl.init_expr) {
      {
        struct ast_ty *result = typecheck_expr(typecheck, ast->toplevel.vdecl.init_expr);
        if (!result) {
          return;
        }
      }

      maybe_implicitly_convert(&ast->toplevel.vdecl.init_expr->ty, &ast->toplevel.vdecl.ty);

      if (!same_type(ast->toplevel.vdecl.init_expr->ty, ast->toplevel.vdecl.ty)) {
        char resultstr[256], tystr[256];
        type_name_into(ast->toplevel.vdecl.init_expr->ty, resultstr, 256);
        type_name_into(ast->toplevel.vdecl.ty, tystr, 256);

        typecheck_diag_expr(typecheck, ast->toplevel.vdecl.init_expr,
                            "variable %s initializer has type %s, expected %s\n",
                            ast->toplevel.vdecl.ident.value.identv.ident, resultstr, tystr);
        ++typecheck->errors;
        return;
      }
    }
  }

  if (ast->type == AST_DECL_TYPE_IMPORT) {
    typecheck_ast(typecheck, ast->toplevel.import.ast);
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
      return typecheck_expr(typecheck, ast->stmt.expr);

    case AST_STMT_TYPE_LET: {
      struct scope_entry *entry = calloc(1, sizeof(struct scope_entry));
      entry->ty = ast->stmt.let.ty;
      entry->decl_flags = ast->stmt.let.flags;

      // if a type was actually specified we need to resolve it
      if (!type_is_tbd(&ast->stmt.let.parser_ty)) {
        ast->stmt.let.ty = resolve_parsed_type(typecheck, &ast->stmt.let.parser_ty);
      } else {
        ast->stmt.let.ty = type_repository_tbd(typecheck->type_repo);
      }

      // insert before checking the initializer to allow recursive references
      scope_insert(typecheck->scope, ast->stmt.let.ident.value.identv.ident, entry);
      {
        struct ast_ty *init_ty = typecheck_expr(typecheck, ast->stmt.let.init_expr);
        if (!init_ty) {
          return NULL;
        }

        if (type_is_tbd(ast->stmt.let.ty)) {
          if (init_ty->flags & TYPE_FLAG_CONSTANT) {
            struct ast_ty new_ty = *init_ty;
            new_ty.flags &= ~TYPE_FLAG_CONSTANT;
            init_ty = type_repository_lookup_ty(typecheck->type_repo, &new_ty);
            if (!init_ty) {
              init_ty = type_repository_register(typecheck->type_repo, &new_ty);
            }
          }

          // inferred type
          ast->stmt.let.ty = init_ty;
        }
      }

      maybe_implicitly_convert(&ast->stmt.let.init_expr->ty, &ast->stmt.let.ty);

      if (!same_type(ast->stmt.let.ty, ast->stmt.let.init_expr->ty)) {
        char tystr[256], initstr[256];
        type_name_into(ast->stmt.let.ty, tystr, 256);
        type_name_into(ast->stmt.let.init_expr->ty, initstr, 256);

        typecheck_diag_expr(typecheck, ast->stmt.let.init_expr,
                            "let %s initializer has type %s, expected %s\n",
                            ast->stmt.let.ident.value.identv.ident, initstr, tystr);
        ++typecheck->errors;
      }

      entry->ty = ast->stmt.let.ty;
    } break;

    case AST_STMT_TYPE_ITER: {
      struct ast_ty *start = typecheck_expr(typecheck, ast->stmt.iter.range.start);
      struct ast_ty *end = typecheck_expr(typecheck, ast->stmt.iter.range.end);
      struct ast_ty *step, step_ty;
      if (ast->stmt.iter.range.step) {
        step = typecheck_expr(typecheck, ast->stmt.iter.range.step);
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

      if (ast->stmt.iter.range.step && !same_type(start, step)) {
        char startstr[256], stepstr[256];
        type_name_into(start, startstr, 256);
        type_name_into(step, stepstr, 256);

        fprintf(stderr, "iter range start has type %s, step has type %s\n", startstr, stepstr);
        ++typecheck->errors;
      }

      // struct ast_vdecl *index = calloc(1, sizeof(struct ast_vdecl));

      struct scope_entry *entry = calloc(1, sizeof(struct scope_entry));
      entry->ty = ast->stmt.iter.range.start->ty;
      // entry->decl_flags = index->flags;
      // ast->stmt.iter.index_vdecl = index;
      ast->stmt.iter.index_ty = entry->ty;

      // new scope for the loop variable
      typecheck->scope = enter_scope(typecheck->scope);

      // insert before checking the initializer to allow recursive references
      scope_insert(typecheck->scope, ast->stmt.iter.index.ident.value.identv.ident, entry);

      if (!typecheck_block(typecheck, &ast->stmt.iter.block)) {
        return NULL;
      }

      typecheck->scope = exit_scope(typecheck->scope);
    } break;

    case AST_STMT_TYPE_STORE: {
      struct ast_ty *lhs = typecheck_expr(typecheck, ast->stmt.store.lhs);
      struct ast_ty *rhs = typecheck_expr(typecheck, ast->stmt.store.rhs);

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
      if (ast->stmt.expr) {
        return typecheck_expr(typecheck, ast->stmt.expr);
      } else {
        return &typecheck->void_type;
      }
    } break;

    case AST_STMT_TYPE_DEFER: {
      if (!typecheck_expr(typecheck, ast->stmt.expr)) {
        return NULL;
      }
      // expression type is irrelevant; defer is a void statement
    } break;

    case AST_STMT_TYPE_WHILE: {
      struct ast_ty *cond = typecheck_expr(typecheck, ast->stmt.while_stmt.cond);
      if (!cond) {
        return NULL;
      }

      // TODO: needs to be an integer condition

      if (!typecheck_block(typecheck, &ast->stmt.while_stmt.block)) {
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
