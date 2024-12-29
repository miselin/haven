/* Desugaring pass for things like generic enums. */

#include "desugar.h"

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

struct desugar_enum {
  struct ast_ty *decl;
  struct ast_tydecl *tydecl;
};

struct desugar {
  struct ast_program *ast;
  struct compiler *compiler;

  struct kv *enums;
  struct kv *specialized;
};

int desugar_ast(struct desugar *desugar, struct ast_program *ast);
// returns 1 if the decl should be removed
static int desugar_toplevel(struct desugar *desugar, struct ast_toplevel *ast);
static int desugar_tydecl(struct desugar *desugar, struct ast_tydecl *ast);
static int desugar_fdecl(struct desugar *desugar, struct ast_fdecl *ast);

// Process a type, desugaring templates if necessary. If templates are desugared, the type
// will become an AST_TYPE_CUSTOM with the mangled name of the specialized type.
static int maybe_desugar_type(struct desugar *desugar, struct ast_ty *ty);

static int desugar_enum(struct desugar *desugar, struct ast_ty *ty);

struct desugar *desugar_new(struct ast_program *ast, struct compiler *compiler) {
  struct desugar *desugar = calloc(1, sizeof(struct desugar));
  desugar->ast = ast;
  desugar->compiler = compiler;
  desugar->enums = new_kv();
  desugar->specialized = new_kv();
  return desugar;
}

int desugar_run(struct desugar *desugar) {
  return desugar_ast(desugar, desugar->ast);
}

void desugar_destroy(struct desugar *desugar) {
  void *iter = kv_iter(desugar->specialized);
  while (!kv_end(iter)) {
    struct desugar_enum *entry = kv_next(&iter);
    free(entry);
  }
  destroy_kv(desugar->specialized);

  iter = kv_iter(desugar->enums);
  while (!kv_end(iter)) {
    struct desugar_enum *desugar_enum = kv_next(&iter);
    free_tydecl(desugar->compiler, desugar_enum->tydecl, 0);
    free(desugar_enum);
  }
  destroy_kv(desugar->enums);

  free(desugar);
}

int desugar_ast(struct desugar *desugar, struct ast_program *ast) {
  struct ast_toplevel *decl = ast->decls;
  struct ast_toplevel *prev = NULL;
  while (decl) {
    int rc = desugar_toplevel(desugar, decl);
    if (rc < 0) {
      return -1;
    } else if (rc > 0) {
      if (prev) {
        prev->next = decl->next;
      } else {
        ast->decls = decl->next;
      }
    } else {
      prev = decl;
    }

    decl = decl->next;
  }

  return 0;
}

static int desugar_toplevel(struct desugar *desugar, struct ast_toplevel *ast) {
  if (ast->type == AST_DECL_TYPE_FDECL) {
    return desugar_fdecl(desugar, &ast->fdecl);
  } else if (ast->type == AST_DECL_TYPE_TYDECL) {
    return desugar_tydecl(desugar, &ast->tydecl);
  }

  return 0;
}

static int desugar_tydecl(struct desugar *desugar, struct ast_tydecl *ast) {
  switch (ast->parsed_ty.ty) {
    case AST_TYPE_ENUM: {
      // yeee
      if (!ast->parsed_ty.enumty.templates) {
        return 0;
      }

      compiler_log(desugar->compiler, LogLevelDebug, "desugar", "desugaring enum %s",
                   ast->ident.value.identv.ident);

      struct desugar_enum *desugar_enum = calloc(1, sizeof(struct desugar_enum));
      desugar_enum->tydecl = ast;
      desugar_enum->decl = &ast->parsed_ty;
      kv_insert(desugar->enums, ast->ident.value.identv.ident, desugar_enum);

      return 1;
    } break;

    default:
      break;
  }

  return 0;
}

static struct ast_template_ty *template_for_name(struct ast_template_ty *decl_tmpls,
                                                 struct ast_template_ty *spec_tmpls,
                                                 const char *name) {
  while (decl_tmpls) {
    if (strcmp(decl_tmpls->name, name) == 0) {
      return spec_tmpls;
    }

    decl_tmpls = decl_tmpls->next;
    spec_tmpls = spec_tmpls->next;
  }

  return NULL;
}

static int desugar_fdecl(struct desugar *desugar, struct ast_fdecl *ast) {
  return maybe_desugar_type(desugar, &ast->parsed_retty);
}

static int maybe_desugar_type(struct desugar *desugar, struct ast_ty *ty) {
  if (ty->ty != AST_TYPE_TEMPLATE) {
    return 0;
  }

  if (ty->tmpl.outer->ty != AST_TYPE_CUSTOM) {
    return 0;
  }

  if (desugar_enum(desugar, ty)) {
    return -1;
  }

  char mangled[256];
  mangle_type(ty, mangled, 256, "specialized.");

  memset(ty, 0, sizeof(struct ast_ty));
  ty->ty = AST_TYPE_CUSTOM;
  strncpy(ty->name, mangled, 256);

  return 0;
}

static int desugar_enum(struct desugar *desugar, struct ast_ty *ty) {
  if (ty->ty != AST_TYPE_TEMPLATE) {
    return 0;
  }

  if (ty->tmpl.outer->ty != AST_TYPE_CUSTOM) {
    return 0;
  }

  char mangled[256];
  mangle_type(ty, mangled, 256, "specialized.");

  struct desugar_enum *spec = kv_lookup(desugar->specialized, mangled);
  if (spec) {
    // already specialized
    return 0;
  }

  // do we know about the existence of this template yet?
  compiler_log(desugar->compiler, LogLevelDebug, "desugar", "checking for enum %s",
               ty->tmpl.outer->name);

  struct desugar_enum *desugar_enum = kv_lookup(desugar->enums, ty->tmpl.outer->name);
  if (!desugar_enum) {
    compiler_log(desugar->compiler, LogLevelError, "desugar", "unknown enum %s in template",
                 ty->tmpl.outer->name);
    return 0;
  }

  compiler_log(desugar->compiler, LogLevelDebug, "desugar", "mangled type %s", mangled);

  // Step 1: Build a new enum type declaration with this specialized type.
  struct ast_toplevel *new_toplevel = calloc(1, sizeof(struct ast_toplevel));
  new_toplevel->type = AST_DECL_TYPE_TYDECL;
  struct ast_tydecl *new_tydecl = &new_toplevel->tydecl;

  strncpy(new_tydecl->ident.value.identv.ident, mangled, 256);
  new_tydecl->parsed_ty = *desugar_enum->decl;
  new_tydecl->parsed_ty.enumty.templates = NULL;

  struct ast_enum_field *field = desugar_enum->decl->enumty.fields;
  struct ast_enum_field *last = NULL;
  while (field) {
    struct ast_enum_field *new_field = calloc(1, sizeof(struct ast_enum_field));
    *new_field = *field;

    if (field->has_inner) {
      if (field->parser_inner.ty == AST_TYPE_CUSTOM) {
        struct ast_template_ty *tmpl = template_for_name(desugar_enum->decl->enumty.templates,
                                                         ty->tmpl.inners, field->parser_inner.name);

        if (tmpl) {
          new_field->parser_inner = tmpl->parsed_ty;
        }
      }
    }

    if (last == NULL) {
      new_tydecl->parsed_ty.enumty.fields = new_field;
    } else {
      last->next = new_field;
    }

    last = new_field;
    field = field->next;
  }

  // Step 2: Insert this new enum type declaration into the AST.
  new_toplevel->next = desugar->ast->decls;
  desugar->ast->decls = new_toplevel;

  // Step 3: Insert into the specialized list.
  spec = calloc(1, sizeof(struct desugar_enum));
  spec->tydecl = new_tydecl;
  spec->decl = &new_tydecl->parsed_ty;
  kv_insert(desugar->specialized, mangled, spec);

  return 0;
}
