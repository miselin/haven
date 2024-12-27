#include <stdarg.h>
#include <string.h>

#include "ast.h"
#include "internal.h"
#include "kv.h"
#include "typecheck.h"
#include "types.h"
#include "utility.h"

struct ast_ty resolve_type(struct typecheck *typecheck, struct ast_ty *ty) {
  if (ty == &typecheck->void_type) {
    return *ty;
  }

  if (ty->ty == AST_TYPE_POINTER || ty->ty == AST_TYPE_BOX) {
    struct ast_ty new_ty = copy_type(ty);
    struct ast_ty resolved = resolve_type(typecheck, ty->pointer.pointee);
    free_ty(new_ty.pointer.pointee, 0);
    *new_ty.pointer.pointee = resolved;
    return new_ty;
  }

  if (ty->ty == AST_TYPE_ARRAY) {
    struct ast_ty new_ty = copy_type(ty);
    struct ast_ty resolved = resolve_type(typecheck, ty->array.element_ty);
    free_ty(new_ty.array.element_ty, 0);
    *new_ty.array.element_ty = resolved;
    return new_ty;
  }

  if (ty->ty == AST_TYPE_ENUM && ty->enumty.templates) {
    struct ast_ty new_ty = copy_type(ty);

    // can we resolve?
    struct ast_template_ty *template = new_ty.enumty.templates;
    while (template) {
      if (!template->is_resolved) {
        template->resolved = type_tbd();
      }

      template = template->next;
    }

    return new_ty;
  }

  if (ty->ty == AST_TYPE_TEMPLATE) {
    // step 1: resolve the outer type as the main return type
    struct ast_ty result = resolve_type(typecheck, ty->tmpl.outer);
    if (result.ty != AST_TYPE_ENUM) {
      // can't template non-enums
      fprintf(stderr, "template outer type is not an enum\n");
      return type_error();
    }

    // step 2: resolve the inner types into the specialized enum type
    struct ast_enum_field *field = result.enumty.fields;
    while (field) {
      if (field->has_inner && field->inner.ty == AST_TYPE_CUSTOM) {
        // match to the template list
        struct ast_template_ty *inner = result.enumty.templates;
        struct ast_template_ty *inner_specific = ty->tmpl.inners;
        while (inner) {
          if (strcmp(inner->name, field->inner.name) == 0) {
            break;
          }
          inner = inner->next;
          inner_specific = inner_specific->next;
        }

        if (!inner) {
          fprintf(stderr, "template %s not found\n", field->inner.name);
          return type_error();
        }

        if (!inner_specific->is_resolved) {
          fprintf(stderr, "template %s is not resolved\n", field->inner.name);
          return type_error();
        }

        field->inner = resolve_type(typecheck, &inner_specific->resolved);
      }
      field = field->next;
    }

    return result;
  }

  if (ty->ty != AST_TYPE_CUSTOM || ty->custom.is_template) {
    return copy_type(ty);
  }

  struct alias_entry *entry = kv_lookup(typecheck->aliases, ty->name);
  if (!entry) {
    return type_error();
  }

  if (entry->ty.ty == AST_TYPE_CUSTOM && entry->ty.custom.is_forward_decl) {
    return *ty;
  }

  if (ty->ty == AST_TYPE_CUSTOM && entry->ty.ty == AST_TYPE_CUSTOM) {
    if (!strcmp(ty->name, entry->ty.name)) {
      fprintf(stderr, "alias loop detected for %s\n", ty->name);
      return type_error();
    }
  }

  if (entry->ty.ty == AST_TYPE_CUSTOM) {
    // recurse until we find a non-alias type
    return resolve_type(typecheck, &entry->ty);
  }

  // copy flags from original type (e.g. ptr); don't mutate original type
  struct ast_ty resolved_type = copy_type(&entry->ty);
  resolved_type.flags |= ty->flags;
  resolved_type.flags |= TYPE_FLAG_INDIRECT;

  return resolved_type;
}

void resolve_template_type(struct typecheck *typecheck, struct ast_template_ty *templates,
                           struct ast_ty *ty) {
  UNUSED(typecheck);

  if (ty->ty != AST_TYPE_CUSTOM) {
    return;
  }

  // match to template types if present
  struct ast_template_ty *template = templates;
  while (template) {
    if (!strcmp(template->name, ty->name)) {
      if (template->is_resolved) {
        free_ty(ty, 0);
        *ty = copy_type(&template->resolved);
      } else {
        ty->custom.is_template = 1;
      }
    }
    template = template->next;
  }
}
