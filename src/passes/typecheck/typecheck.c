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
#include "utility.h"

int typecheck_verify_ast(struct ast_program *ast);
int typecheck_implicit_ast(struct ast_program *ast);

static void typecheck_ast(struct typecheck *typecheck, struct ast_program *ast, int only_tydecls);
static void typecheck_toplevel(struct typecheck *typecheck, struct ast_toplevel *ast,
                               int only_tydecls);
static struct ast_ty *typecheck_stmt(struct typecheck *typecheck, struct ast_stmt *ast);

static void typecheck_struct_decl(struct typecheck *typecheck, struct ast_ty *decl);
static void typecheck_enum_decl(struct typecheck *typecheck, struct ast_ty *decl);

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
  // do the first pass with just type declarations so we can fully resolve forward-declared types
  // typecheck_ast(typecheck, typecheck->ast, 1);
  typecheck_ast(typecheck, typecheck->ast, 0);
  if (typecheck->errors) {
    return typecheck->errors;
  }
#if 0
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
  if (typecheck_verify_ast(typecheck->ast) < 0) {
    return 1;
  }
#endif
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

static void typecheck_ast(struct typecheck *typecheck, struct ast_program *ast, int only_tydecls) {
  struct ast_toplevel *decl = ast->decls;
  while (decl) {
    typecheck_toplevel(typecheck, decl, only_tydecls);
    decl = decl->next;
  }
}

static void typecheck_toplevel(struct typecheck *typecheck, struct ast_toplevel *ast,
                               int only_tydecls) {
  if (ast->type == AST_DECL_TYPE_TYDECL) {
    if (ast->tydecl.parsed_ty.ty == AST_TYPE_CUSTOM) {
      // custom - trying to make an alias
      struct ast_ty *target_ty =
          type_repository_lookup(typecheck->type_repo, ast->tydecl.parsed_ty.name);
      if (!target_ty) {
        fprintf(stderr, "type %s not found [%s]\n", ast->tydecl.parsed_ty.name,
                ast->tydecl.ident.value.identv.ident);
        ++typecheck->errors;
        return;
      }

      if (!type_repository_register_alias(
              typecheck->type_repo, ast->tydecl.ident.value.identv.ident, &ast->tydecl.parsed_ty)) {
        ++typecheck->errors;
      }

      ast->tydecl.resolved = target_ty;
      return;
    }

    // not custom, defining a new type
    if (ast->tydecl.parsed_ty.ty == AST_TYPE_STRUCT) {
      typecheck_struct_decl(typecheck, &ast->tydecl.parsed_ty);
    } else if (ast->tydecl.parsed_ty.ty == AST_TYPE_ENUM) {
      typecheck_enum_decl(typecheck, &ast->tydecl.parsed_ty);
    } else if (ast->tydecl.parsed_ty.ty == AST_TYPE_ARRAY) {
      // TODO
      struct ast_ty *resolved =
          resolve_parsed_type(typecheck, ast->tydecl.parsed_ty.array.element_ty);
      free_ty(typecheck->compiler, ast->tydecl.parsed_ty.array.element_ty, 1);
      ast->tydecl.parsed_ty.array.element_ty = resolved;
    } /*else if (ast->tydecl.ty.ty == AST_TYPE_FUNCTION) {
      // TODO
      struct ast_ty resolved = resolve_type(typecheck, ast->tydecl.ty.function.retty);
      free_ty(ast->tydecl.ty.function.retty, 0);
      *ast->tydecl.ty.function.retty = resolved;

      for (size_t i = 0; i < ast->tydecl.ty.function.num_args; i++) {
        struct ast_ty arg_resolved = resolve_type(typecheck, ast->tydecl.ty.function.args[i]);
        free_ty(ast->tydecl.ty.function.args[i], 0);
        *ast->tydecl.ty.function.args[i] = arg_resolved;
      }
    } */
    else {
      ast->tydecl.resolved = resolve_parsed_type(typecheck, &ast->tydecl.parsed_ty);
    }

    strncpy(ast->tydecl.parsed_ty.name, ast->tydecl.ident.value.identv.ident, 256);

    struct ast_ty *registered =
        type_repository_register(typecheck->type_repo, &ast->tydecl.parsed_ty);
    if (registered) {
      type_repository_register_alias(typecheck->type_repo, ast->tydecl.ident.value.identv.ident,
                                     registered);
    }

    ast->tydecl.resolved = registered;
  }

  if (only_tydecls) {
    return;
  }

  if (ast->type == AST_DECL_TYPE_FDECL) {
    /*
    struct ast_ty resolved = resolve_type(typecheck, &ast->fdecl.retty);
    free_ty(&ast->fdecl.retty, 0);
    ast->fdecl.retty = resolved;
    */
    ast->fdecl.retty = resolve_type(typecheck, &ast->fdecl.parsed_retty);
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
      entry->fdecl->params[i]->ty = resolve_type(typecheck, &entry->fdecl->params[i]->parser_ty);

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

      struct ast_ty *result = typecheck_block(typecheck, ast->fdecl.body);

      typecheck->scope = exit_scope(typecheck->scope);

      if (!result) {
        return;
      }

      maybe_implicitly_convert(&ast->fdecl.body->ty, &ast->fdecl.retty);

      if (!same_type(result, ast->fdecl.retty)) {
        char resultstr[256], tystr[256];
        type_name_into(result, resultstr, 256);
        type_name_into(ast->fdecl.retty, tystr, 256);

        fprintf(stderr, "function %s returns %s, expected %s\n",
                ast->fdecl.ident.value.identv.ident, resultstr, tystr);
        ++typecheck->errors;
        return;
      }
    }
  } else if (ast->type == AST_DECL_TYPE_VDECL) {
    ast->vdecl.ty = resolve_type(typecheck, &ast->vdecl.parser_ty);
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
      struct ast_ty *result = typecheck_expr(typecheck, ast->vdecl.init_expr);
      if (!result) {
        return;
      }

      maybe_implicitly_convert(&ast->vdecl.init_expr->ty, &ast->vdecl.ty);

      if (!same_type(result, ast->vdecl.ty)) {
        char resultstr[256], tystr[256];
        type_name_into(result, resultstr, 256);
        type_name_into(ast->vdecl.ty, tystr, 256);

        typecheck_diag_expr(typecheck, ast->vdecl.init_expr,
                            "variable %s initializer has type %s, expected %s\n",
                            ast->vdecl.ident.value.identv.ident, resultstr, tystr);
        ++typecheck->errors;
        return;
      }
    }
  }
}

static void typecheck_struct_decl(struct typecheck *typecheck, struct ast_ty *decl) {
  UNUSED(typecheck);

  // TODO: causes infinite recursive loop on recursive struct definitions

  struct ast_struct_field *field = decl->structty.fields;
  while (field) {
    // TODO: check for recursive definition, ensure it's a pointer if so, or it's not representable
    field->ty = resolve_parsed_type(typecheck, &field->parsed_ty);
    field = field->next;
  }
}

static void typecheck_enum_decl(struct typecheck *typecheck, struct ast_ty *decl) {
  struct ast_enum_field *field = decl->enumty.fields;
  while (field) {
    if (field->has_inner) {
      resolve_template_type(typecheck, decl->enumty.templates, &field->parser_inner);
      field->inner = resolve_parsed_type(typecheck, &field->parser_inner);
    }
    field = field->next;
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

  ast->ty = resolve_type(typecheck, last_ty);
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

      struct ast_ty *init_ty = typecheck_expr(typecheck, ast->let.init_expr);
      if (!init_ty) {
        return NULL;
      }

      if (type_is_tbd(ast->let.ty)) {
        // inferred type
        ast->let.ty = init_ty;
      }

      maybe_implicitly_convert(&ast->let.init_expr->ty, &ast->let.ty);

      if (!same_type(ast->let.ty, init_ty)) {
        char tystr[256], initstr[256];
        type_name_into(ast->let.ty, tystr, 256);
        type_name_into(init_ty, initstr, 256);

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
        step = &step_ty;
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
      entry->vdecl->parser_ty = *start;
      ast->iter.index_vdecl = entry->vdecl;

      // new scope for the loop variable
      typecheck->scope = enter_scope(typecheck->scope);

      // insert before checking the initializer to allow recursive references
      scope_insert(typecheck->scope, ast->iter.index.ident.value.identv.ident, entry);

      typecheck_block(typecheck, &ast->iter.block);

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

      typecheck_block(typecheck, &ast->while_stmt.block);
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
