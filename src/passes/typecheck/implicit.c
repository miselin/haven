#include <stdarg.h>
#include <string.h>

#include "ast.h"
#include "internal.h"
#include "types.h"

int maybe_implicitly_convert(struct ast_ty **into_from, struct ast_ty **into_to) {
  struct ast_ty *from = *into_from;
  struct ast_ty *to = *into_to;

  if (type_is_tbd(to)) {
    *into_to = *into_from;
    return 1;
  }

  if (from->ty == AST_TYPE_NIL) {
    // nil can be coerced to any type
    *into_from = *into_to;
    return 1;
  }

  if (!compatible_types(from, to)) {
    // no-op
    return 0;
  }

  if (from->ty == AST_TYPE_INTEGER && to->ty == AST_TYPE_INTEGER) {
    if (from->integer.width == to->integer.width) {
      return 0;
    }

    if (from->flags & TYPE_FLAG_CONSTANT && to->flags & TYPE_FLAG_CONSTANT) {
      int conversion = from->integer.width != to->integer.width;

      // TODO: there be some additional checks around signed/unsigned conversion
      // e.g. make sure the conversion doesn't change the sign
      // swap from/to so the result is the highest width
      if (from->integer.width > to->integer.width) {
        *into_to = *into_from;
      } else {
        *into_from = *into_to;
      }

      return conversion;
    }

    // don't propagate constant type in the wrong direction
    if (to->flags & TYPE_FLAG_CONSTANT) {
      return 0;
    }

    // convert the source width to the destinaiton width
    *into_from = *into_to;
    return 1;
  } else if (from->ty == AST_TYPE_ENUM && to->ty == AST_TYPE_ENUM) {
    if (!(from->enumty.templates)) {
      // no implicit conversion, source must have templates
      return 0;
    }

    if (strcmp(from->name, to->specialization_of ? to->specialization_of : to->name) != 0) {
      // no implicit conversion, enum name must match
      return 0;
    }

    // TODO: this is broken by the type repository work; need a way to resolve the field
#if 0
    int coerced = 0;

    // ensure the most specific type is used (e.g. resolved type)
    struct ast_enum_field *from_field = from->enumty.fields;
    struct ast_enum_field *to_field = to->enumty.fields;
    while (from_field && to_field) {
      if (from_field->has_inner && to_field->has_inner) {
        if (from_field->inner->ty == AST_TYPE_CUSTOM && to_field->inner->ty != AST_TYPE_CUSTOM) {
          // bring across the resolved type
          // TODO
          // free_ty(&from_field->inner, 0);
          // from_field->inner = copy_type(&to_field->inner);
          coerced = 1;
        }
      }

      from_field = from_field->next;
      to_field = to_field->next;
    }

    if (coerced) {
      // copy names and specialization to allow further implicit conversions
      strncpy(from->name, to->name, sizeof(from->name));
      if (to->specialization_of) {
        from->specialization_of = strdup(to->specialization_of);
      }
    }

    return coerced;
#endif
  }

  return 0;
}
