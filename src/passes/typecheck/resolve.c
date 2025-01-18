#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "compiler.h"
#include "internal.h"
#include "kv.h"
#include "typecheck.h"
#include "types.h"
#include "utility.h"

struct ast_ty *resolve_type(struct typecheck *typecheck, struct ast_ty *ty) {
  if (ty->ty == AST_TYPE_VOID) {
    return type_repository_void(typecheck->type_repo);
  } else if (ty->ty == AST_TYPE_ERROR) {
    return type_repository_error(typecheck->type_repo);
  } else if (ty->ty == AST_TYPE_TBD) {
    return type_repository_tbd(typecheck->type_repo);
  }

  struct ast_ty *target = type_repository_lookup_ty(typecheck->type_repo, ty);
  if (target) {
    return target;
  }

  // TODO: template types? other fancy types?
  return type_repository_register(typecheck->type_repo, ty);

  char type_name[1024];
  type_name_into(ty, type_name, 1024);

  // TODO: need to actually resolve / register the type
  compiler_log(typecheck->compiler, LogLevelError, "typecheck",
               "resolve_type not yet implemented beyond lookups in this new world (for %s)",
               type_name);

  return NULL;
}

struct ast_ty *resolve_parsed_type(struct typecheck *typecheck, struct ast_ty *ty) {
  if (ty->ty == AST_TYPE_VOID) {
    return type_repository_void(typecheck->type_repo);
  } else if (ty->ty == AST_TYPE_ERROR) {
    return type_repository_error(typecheck->type_repo);
  } else if (ty->ty == AST_TYPE_TBD) {
    return type_repository_tbd(typecheck->type_repo);
  }

  struct ast_ty *resolved_ty = calloc(1, sizeof(struct ast_ty));
  *resolved_ty = *ty;

  // resolve inner parsed types to actual types
  switch (ty->ty) {
    case AST_TYPE_ERROR:
    case AST_TYPE_TBD:
    case AST_TYPE_INTEGER:
    case AST_TYPE_STRING:
    case AST_TYPE_FLOAT:
    case AST_TYPE_FVEC:
    case AST_TYPE_VOID:
    case AST_TYPE_CUSTOM:
    case AST_TYPE_NIL:
    case AST_TYPE_MATRIX:
      // no complex data to copy
      break;

    case AST_TYPE_ARRAY:
      resolved_ty->array.element_ty = resolve_parsed_type(typecheck, ty->array.element_ty);
      compiler_log(typecheck->compiler, LogLevelDebug, "typecheck",
                   "resolved array element type for %p to %p", (void *)resolved_ty,
                   (void *)resolved_ty->array.element_ty);
      break;

    case AST_TYPE_STRUCT: {
      resolved_ty->structty.fields = NULL;
      struct ast_struct_field *field = ty->structty.fields;
      struct ast_struct_field *last = NULL;
      while (field) {
        struct ast_struct_field *new_field = calloc(1, sizeof(struct ast_struct_field));
        *new_field = *field;
        new_field->ty = resolve_parsed_type(typecheck, &field->parsed_ty);
        compiler_log(typecheck->compiler, LogLevelDebug, "typecheck",
                     "resolved type for struct field %s to %p", field->name, (void *)new_field->ty);
        field = field->next;

        if (last == NULL) {
          resolved_ty->structty.fields = new_field;
        } else {
          last->next = new_field;
        }

        last = new_field;
      }
    } break;

    case AST_TYPE_ENUM: {
      struct ast_enum_field *field = ty->enumty.fields;
      struct ast_enum_field *last = NULL;
      while (field) {
        struct ast_enum_field *new_field = calloc(1, sizeof(struct ast_enum_field));
        *new_field = *field;
        if (field->has_inner) {
          resolve_template_type(typecheck, ty->enumty.templates, &field->parser_inner);
          new_field->inner = resolve_parsed_type(typecheck, &field->parser_inner);
          compiler_log(typecheck->compiler, LogLevelDebug, "typecheck",
                       "resolved inner type for enum field %s to %p", field->name,
                       (void *)new_field->inner);
        }
        field = field->next;

        if (last == NULL) {
          resolved_ty->enumty.fields = new_field;
        } else {
          last->next = new_field;
        }

        last = new_field;
      }

      struct ast_template_ty *template = ty->enumty.templates;
      while (template) {
        if (!template->resolved) {
          template->resolved = type_repository_tbd(typecheck->type_repo);
        }

        template = template->next;
      }
    } break;

    case AST_TYPE_TEMPLATE: {
      resolved_ty->tmpl.outer = resolve_parsed_type(typecheck, ty->tmpl.outer);
      if (resolved_ty->tmpl.outer->ty != AST_TYPE_ENUM) {
        compiler_log(typecheck->compiler, LogLevelError, "typecheck",
                     "template outer type must be an enum, got %d", resolved_ty->tmpl.outer->ty);
        return type_repository_error(typecheck->type_repo);
      }

      // step 2: resolve the inner types into the specialized enum type
      struct ast_enum_field *field = resolved_ty->tmpl.outer->enumty.fields;
      while (field) {
        if (field->has_inner && field->inner->ty == AST_TYPE_CUSTOM) {
          // match to the template list
          struct ast_template_ty *inner = resolved_ty->tmpl.outer->enumty.templates;
          struct ast_template_ty *inner_specific = ty->tmpl.inners;
          while (inner) {
            if (strcmp(inner->name, field->inner->name) == 0) {
              break;
            }
            inner = inner->next;
            inner_specific = inner_specific->next;
          }

          if (!inner) {
            fprintf(stderr, "template %s not found\n", field->inner->name);
            return type_repository_error(typecheck->type_repo);
          }

          if (!inner_specific->is_resolved) {
            fprintf(stderr, "template %s is not resolved\n", field->inner->name);
            return type_repository_error(typecheck->type_repo);
          }

          field->inner = resolve_type(typecheck, inner_specific->resolved);
        }
        field = field->next;
      }
    } break;

    case AST_TYPE_FUNCTION: {
      resolved_ty->function.param_types = calloc(ty->function.num_params, sizeof(struct ast_ty *));
      for (size_t i = 0; i < ty->function.num_params; i++) {
        resolved_ty->function.param_types[i] =
            resolve_parsed_type(typecheck, ty->function.param_types[i]);
      }

      resolved_ty->function.retty = resolve_parsed_type(typecheck, ty->function.retty);
    } break;

    case AST_TYPE_POINTER:
    case AST_TYPE_BOX:
      resolved_ty->pointer.pointee = resolve_parsed_type(typecheck, ty->pointer.pointee);
      if (!resolved_ty->pointer.pointee) {
        resolved_ty->pointer.pointee = type_repository_error(typecheck->type_repo);
      }
      break;
  }

  // already exists?
  compiler_log(typecheck->compiler, LogLevelDebug, "typecheck", "resolve_parsed_type lookup...");
  struct ast_ty *target = type_repository_lookup_ty(typecheck->type_repo, resolved_ty);
  compiler_log(typecheck->compiler, LogLevelDebug, "typecheck", "got %p...", (void *)target);
  if (!target && resolved_ty->ty != AST_TYPE_CUSTOM) {
    // register the new type with resolved inner fields, which copies the type
    // customs can't be registered in the type repo - they should have been resolved
    target = type_repository_register(typecheck->type_repo, resolved_ty);
  }

  // done with the resolved type now
  free_ty(typecheck->compiler, resolved_ty, 1);
  compiler_log(typecheck->compiler, LogLevelDebug, "typecheck", "resolved type %p to %p",
               (void *)ty, (void *)target);
  return target;
}

void patch_type_tbds(struct typecheck *typecheck, struct ast_ty *ty, struct ast_ty *parser_ty) {
  UNUSED(parser_ty);

  compiler_log(typecheck->compiler, LogLevelDebug, "typecheck", "patching type %p [%d]", (void *)ty,
               ty->ty);

  // resolve inner parsed types to actual types
  switch (ty->ty) {
    case AST_TYPE_ERROR:
    case AST_TYPE_TBD:
    case AST_TYPE_INTEGER:
    case AST_TYPE_STRING:
    case AST_TYPE_FLOAT:
    case AST_TYPE_FVEC:
    case AST_TYPE_VOID:
    case AST_TYPE_CUSTOM:
    case AST_TYPE_NIL:
    case AST_TYPE_MATRIX:
      // no complex data to copy
      break;

    case AST_TYPE_ARRAY:
      compiler_log(typecheck->compiler, LogLevelDebug, "typecheck",
                   "patching array element type for %p which is %p [%d]", (void *)ty,
                   (void *)ty->array.element_ty, ty->array.element_ty->ty);
      if (ty->array.element_ty->ty == AST_TYPE_CUSTOM) {
        ty->array.element_ty =
            type_repository_lookup(typecheck->type_repo, ty->array.element_ty->name);
      } else {
        patch_type_tbds(typecheck, ty->array.element_ty, NULL);
      }
      break;

    case AST_TYPE_STRUCT: {
      struct ast_struct_field *field = ty->structty.fields;
      while (field) {
        if (field->ty->ty == AST_TYPE_CUSTOM) {
          field->ty = type_repository_lookup(typecheck->type_repo, field->ty->name);
        } else {
          patch_type_tbds(typecheck, field->ty, NULL);
        }

        field = field->next;
      }
    } break;

    case AST_TYPE_ENUM: {
      struct ast_enum_field *field = ty->enumty.fields;
      while (field) {
        if (field->has_inner) {
          compiler_log(typecheck->compiler, LogLevelDebug, "typecheck",
                       "patching inner type for enum field %s which is %p", field->name,
                       (void *)field->inner);
          if (field->inner->ty == AST_TYPE_CUSTOM) {
            field->inner = type_repository_lookup(typecheck->type_repo, field->inner->name);
          } else {
            patch_type_tbds(typecheck, field->inner, NULL);
          }
        }

        field = field->next;
      }

      // TODO: templates
    } break;

    case AST_TYPE_TEMPLATE: {
      // TODO
    } break;

    case AST_TYPE_FUNCTION: {
      for (size_t i = 0; i < ty->function.num_params; i++) {
        if (ty->function.param_types[i]->ty == AST_TYPE_CUSTOM) {
          ty->function.param_types[i] =
              type_repository_lookup(typecheck->type_repo, ty->function.param_types[i]->name);
        } else {
          patch_type_tbds(typecheck, ty->function.param_types[i], NULL);
        }
      }

      if (ty->function.retty->ty == AST_TYPE_CUSTOM) {
        ty->function.retty = type_repository_lookup(typecheck->type_repo, ty->function.retty->name);
      } else {
        patch_type_tbds(typecheck, ty->function.retty, NULL);
      }
    } break;

    case AST_TYPE_POINTER:
    case AST_TYPE_BOX:
      if (ty->pointer.pointee->ty == AST_TYPE_CUSTOM) {
        ty->pointer.pointee =
            type_repository_lookup(typecheck->type_repo, ty->pointer.pointee->name);
      }
      break;
  }

  // register the patched type for use elsewhere
  if (!type_repository_lookup_ty(typecheck->type_repo, ty)) {
    type_repository_register(typecheck->type_repo, ty);
  }
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
        // free_ty(typecheck->compiler, ty, 0);
        // TODO
        // *ty = copy_type(&template->resolved);
      } else {
        ty->custom.is_template = 1;
      }

      break;
    }
    template = template->next;
  }
}
