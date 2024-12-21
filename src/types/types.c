#include "types.h"

#include <stdlib.h>
#include <string.h>

#include "typecheck.h"

struct ast_ty type_tbd(void) {
  struct ast_ty ty;
  memset(&ty, 0, sizeof(ty));
  ty.ty = AST_TYPE_TBD;
  return ty;
}

struct ast_ty type_void(void) {
  struct ast_ty ty;
  memset(&ty, 0, sizeof(ty));
  ty.ty = AST_TYPE_VOID;
  return ty;
}

struct ast_ty type_error(void) {
  struct ast_ty ty;
  memset(&ty, 0, sizeof(ty));
  ty.ty = AST_TYPE_ERROR;
  return ty;
}

int type_is_error(struct ast_ty *ty) {
  return ty->ty == AST_TYPE_ERROR;
}

int type_is_tbd(struct ast_ty *ty) {
  return ty->ty == AST_TYPE_TBD;
}

int type_is_nil(struct ast_ty *ty) {
  return ty->ty == AST_TYPE_NIL;
}

int type_is_constant(struct ast_ty *ty) {
  return ty->flags & TYPE_FLAG_CONSTANT;
}

int type_is_complex(struct ast_ty *ty) {
  // pointers are not complex even if their targets are
  if (ty->flags & TYPE_FLAG_PTR) {
    return 0;
  }

  if (ty->ty == AST_TYPE_STRUCT || ty->ty == AST_TYPE_ARRAY) {
    return 1;
  }

  if (ty->ty == AST_TYPE_ENUM && !ty->enumty.no_wrapped_fields) {
    return 1;
  }

  return 0;
}

int same_type_class(struct ast_ty *ty1, struct ast_ty *ty2, uint64_t flagmask) {
  uint64_t flags1 = ty1->flags & flagmask;
  uint64_t flags2 = ty2->flags & flagmask;

  int same = (ty1->ty == ty2->ty) && (flags1 == flags2);
  if (ty1->ty == AST_TYPE_ARRAY) {
    same = same && same_type_class(ty1->array.element_ty, ty2->array.element_ty, flagmask);
  }

  if (same) {
    return same;
  }

  if (ty1->ty == AST_TYPE_STRING || ty2->ty == AST_TYPE_STRING) {
    // struct ast_ty *strty = ty1->ty == AST_TYPE_STRING ? ty1 : ty2;
    struct ast_ty *otherty = ty1->ty == AST_TYPE_STRING ? ty2 : ty1;

    // strings are identical to i8*
    if (!(otherty->flags & TYPE_FLAG_PTR)) {
      return 0;
    }

    if (otherty->ty == AST_TYPE_INTEGER && otherty->integer.width == 8) {
      return 1;
    }
  }

  return same;
}

int compatible_types(struct ast_ty *ty1, struct ast_ty *ty2) {
  if (same_type(ty1, ty2)) {
    return 1;
  }

  if (type_is_constant(ty1) != type_is_constant(ty2)) {
    // make sure ty1 is the non-constant type
    if (type_is_constant(ty1)) {
      struct ast_ty *tmp = ty1;
      ty1 = ty2;
      ty2 = tmp;
    }
  }

  if (type_is_constant(ty1) && type_is_constant(ty2)) {
    // final constant type is the largest size of the two
    return 1;
  }

  switch (ty1->ty) {
    case AST_TYPE_INTEGER:
      if (ty1->integer.is_signed != ty2->integer.is_signed) {
        return 0;
      }

      // destination type must be at least large enough for the constant
      if (type_is_constant(ty2)) {
        return ty1->integer.width >= ty2->integer.width;
      }

      return ty1->integer.width == ty2->integer.width;

    default:
      return 0;
  }
}

int same_type_masked(struct ast_ty *ty1, struct ast_ty *ty2, uint64_t flagmask) {
  if (!same_type_class(ty1, ty2, flagmask)) {
    return 0;
  }

  switch (ty1->ty) {
    case AST_TYPE_INTEGER:
      if (ty1->integer.is_signed != ty2->integer.is_signed) {
        return 0;
      }

      return ty1->integer.width == ty2->integer.width;
    case AST_TYPE_FVEC:
      return ty1->fvec.width == ty2->fvec.width;
    case AST_TYPE_ARRAY:
      return ty1->array.width == ty2->array.width &&
             same_type_masked(ty1->array.element_ty, ty2->array.element_ty, flagmask);
    default:
      return 1;
  }
}

int same_type(struct ast_ty *ty1, struct ast_ty *ty2) {
  return same_type_masked(ty1, ty2, TYPE_FLAG_MASK_ALL);
}

int narrower_type(struct ast_ty *ty1, struct ast_ty *ty2) {
  if (!same_type_class(ty1, ty2, TYPE_FLAG_MASK_ALL)) {
    return 0;
  }

  switch (ty1->ty) {
    case AST_TYPE_INTEGER:
      return ty1->integer.width < ty2->integer.width;
    case AST_TYPE_FVEC:
      return ty1->fvec.width < ty2->fvec.width;
    case AST_TYPE_ARRAY:
      return narrower_type(ty1->array.element_ty, ty2->array.element_ty);
    default:
      return 0;
  }
}

int wider_type(struct ast_ty *ty1, struct ast_ty *ty2) {
  if (!same_type_class(ty1, ty2, TYPE_FLAG_MASK_ALL)) {
    return 0;
  }

  switch (ty1->ty) {
    case AST_TYPE_INTEGER:
      return ty1->integer.width > ty2->integer.width;
    case AST_TYPE_FVEC:
      return ty1->fvec.width > ty2->fvec.width;
    case AST_TYPE_ARRAY:
      return wider_type(ty1->array.element_ty, ty2->array.element_ty);
    default:
      return 0;
  }
}

const char *type_name(struct ast_ty *ty) {
  static char buf[256];
  type_name_into(ty, buf, 256);
  return buf;
}

int type_name_into(struct ast_ty *ty, char *buf, size_t maxlen) {
  if (!ty) {
    return snprintf(buf, maxlen, "<null-type-ptr>");
  }

  int offset = 0;
  switch (ty->ty) {
    case AST_TYPE_ERROR:
      offset += snprintf(buf, maxlen, "error");
      return offset;
    case AST_TYPE_TBD:
      offset += snprintf(buf, maxlen, "tbd");
      return offset;
    case AST_TYPE_INTEGER:
      if (ty->integer.is_signed) {
        offset += snprintf(buf + offset, maxlen - (size_t)offset, "i");
      } else {
        offset += snprintf(buf + offset, maxlen - (size_t)offset, "u");
      }
      offset += snprintf(buf + offset, maxlen - (size_t)offset, "%zd", ty->integer.width);
      break;
    case AST_TYPE_STRING:
      offset += snprintf(buf, maxlen, "str");
      break;
    case AST_TYPE_FLOAT:
      offset += snprintf(buf, maxlen, "float");
      break;
    case AST_TYPE_FVEC:
      offset += snprintf(buf + offset, maxlen - (size_t)offset, "fvec%zd", ty->fvec.width);
      break;
    case AST_TYPE_VOID:
      offset += snprintf(buf, maxlen, "void");
      break;
    case AST_TYPE_ARRAY: {
      char element_ty[256];
      type_name_into(ty->array.element_ty, element_ty, 256);
      offset +=
          snprintf(buf + offset, maxlen - (size_t)offset, "%s[%zu]", element_ty, ty->array.width);
    } break;
    case AST_TYPE_CUSTOM:
      offset += snprintf(buf, maxlen, "Ty(%s)", ty->name);
      break;
    case AST_TYPE_STRUCT: {
      offset += snprintf(buf, maxlen, "struct %s { ", ty->name);
      struct ast_struct_field *field = ty->structty.fields;
      while (field) {
        if (!strcmp(field->ty->name, ty->name)) {
          // recursive def
          offset += snprintf(buf + offset, maxlen - (size_t)offset, "struct %s%s; ",
                             field->ty->flags & TYPE_FLAG_PTR ? "*" : "", ty->name);
        } else {
          char field_ty[256];
          type_name_into(field->ty, field_ty, 256);
          offset +=
              snprintf(buf + offset, maxlen - (size_t)offset, "%s %s; ", field_ty, field->name);
        }
        field = field->next;
      }
      offset += snprintf(buf + offset, maxlen - (size_t)offset, "}");
    } break;
    case AST_TYPE_NIL:
      offset += snprintf(buf, maxlen, "nil");
      break;
    case AST_TYPE_TEMPLATE:
      offset += snprintf(buf, maxlen, "template ");
      offset += type_name_into(ty->template.outer, buf + offset, maxlen - (size_t)offset);
      offset += snprintf(buf + offset, maxlen - (size_t)offset, "<");
      struct ast_template_ty *inner = ty->template.inners;
      while (inner) {
        offset += snprintf(buf + offset, maxlen - (size_t)offset, "%s %d -> ", inner->name,
                           inner->is_resolved);
        offset += type_name_into(&inner->resolved, buf + offset, maxlen - (size_t)offset);
        if (inner->next) {
          offset += snprintf(buf + offset, maxlen - (size_t)offset, ", ");
        }
        inner = inner->next;
      }
      offset += snprintf(buf + offset, maxlen - (size_t)offset, ">");
      break;
    case AST_TYPE_ENUM:
      offset += snprintf(buf, maxlen, "enum %s <", ty->name);
      struct ast_template_ty *template = ty->enumty.templates;
      while (template) {
        offset += snprintf(buf + offset, maxlen - (size_t)offset, "%s (%s)", template->name,
                           template->is_resolved ? "resolved" : "unresolved");
        if (template->next) {
          offset += snprintf(buf + offset, maxlen - (size_t)offset, ", ");
        }
        template = template->next;
      }

      offset += snprintf(buf + offset, maxlen - (size_t)offset, "> { ");
      struct ast_enum_field *field = ty->enumty.fields;
      while (field) {
        offset +=
            snprintf(buf + offset, maxlen - (size_t)offset, "%s = %ld", field->name, field->value);
        if (field->has_inner) {
          offset += snprintf(buf + offset, maxlen - (size_t)offset, " (");
          offset += type_name_into(&field->inner, buf + offset, maxlen - (size_t)offset);
          offset += snprintf(buf + offset, maxlen - (size_t)offset, ")");
        }
        if (field->next) {
          offset += snprintf(buf + offset, maxlen - (size_t)offset, ", ");
        }
        field = field->next;
      }

      offset += snprintf(buf + offset, maxlen - (size_t)offset, "}");

      break;
    default:
      offset += snprintf(buf, maxlen, "<unknown-type %d>", ty->ty);
      return offset;
  }

  if (ty->flags & TYPE_FLAG_PTR) {
    offset += snprintf(buf + offset, maxlen - (size_t)offset, "*");
  }
  if (ty->flags & TYPE_FLAG_CONSTANT) {
    offset += snprintf(buf + offset, maxlen - (size_t)offset, " const");
  }
  if (ty->flags & ~(TYPE_FLAG_PTR | TYPE_FLAG_CONSTANT)) {
    offset += snprintf(buf + offset, maxlen - (size_t)offset, " (flags %lx)", ty->flags);
  }

  buf[offset] = '\0';
  return offset;
}

int can_cast(struct ast_ty *ty1, struct ast_ty *ty2) {
  // identical types?
  if (same_type(ty1, ty2)) {
    return 1;
  } else if (same_type_class(ty1, ty2, TYPE_FLAG_MASK_ALL)) {
    return 1;
  } else if (compatible_types(ty1, ty2)) {
    return 1;
  }

  // float <> int
  if ((ty1->ty == AST_TYPE_INTEGER && ty2->ty == AST_TYPE_FLOAT) ||
      (ty1->ty == AST_TYPE_FLOAT && ty2->ty == AST_TYPE_INTEGER)) {
    return 1;
  }

  return 0;
}

size_t type_size(struct ast_ty *ty) {
  if (ty->flags & TYPE_FLAG_PTR) {
    // TODO: 64-bit assumption
    return 8;
  }

  switch (ty->ty) {
    case AST_TYPE_INTEGER:
      return ty->integer.width / 8;
    case AST_TYPE_FLOAT:
      return 4;
    case AST_TYPE_FVEC:
      return ty->fvec.width * 4;
    case AST_TYPE_ARRAY:
      return ty->array.width * type_size(ty->array.element_ty);
    case AST_TYPE_STRUCT: {
      size_t size = 0;
      struct ast_struct_field *field = ty->structty.fields;
      while (field) {
        size += type_size(field->ty);
        field = field->next;
      }
      return size;
    }
    case AST_TYPE_ENUM: {
      if (ty->enumty.no_wrapped_fields) {
        return 4;  // tag
      }

      size_t size = 4;  // tag
      struct ast_enum_field *field = ty->enumty.fields;
      size_t largest_inner = 0;
      while (field) {
        if (field->has_inner) {
          size_t tysz = type_size(&field->inner);
          if (tysz > largest_inner) {
            largest_inner = tysz;
          }
        }
        field = field->next;
      }

      size += largest_inner;

      return size;
    } break;

    case AST_TYPE_CUSTOM:
      // unresolved, emit the smallest possible storage for it
      return 1;

    default:
      fprintf(stderr, "type_size unhandled %d [%s]\n", ty->ty, ty->name);
      return 0;
  }
}

struct ast_ty copy_type(struct ast_ty *ty) {
  struct ast_ty new_type = *ty;

  if (ty->specialization_of) {
    new_type.specialization_of = strdup(ty->specialization_of);
  }

  if (ty->ty == AST_TYPE_ARRAY) {
    new_type.array.element_ty = calloc(1, sizeof(struct ast_ty));
    *new_type.array.element_ty = copy_type(ty->array.element_ty);
  } else if (ty->ty == AST_TYPE_STRUCT) {
    new_type.structty.fields = NULL;
    struct ast_struct_field *field = ty->structty.fields;
    struct ast_struct_field *last = NULL;
    while (field) {
      struct ast_struct_field *new_field = calloc(1, sizeof(struct ast_struct_field));
      *new_field = *field;
      new_field->ty = calloc(1, sizeof(struct ast_ty));
      *new_field->ty = copy_type(field->ty);
      field = field->next;

      if (last == NULL) {
        new_type.structty.fields = new_field;
      } else {
        last->next = new_field;
      }

      last = new_field;
    }
  } else if (ty->ty == AST_TYPE_ENUM) {
    struct ast_enum_field *field = ty->enumty.fields;
    struct ast_enum_field *last = NULL;
    while (field) {
      struct ast_enum_field *new_field = calloc(1, sizeof(struct ast_enum_field));
      *new_field = *field;
      if (field->has_inner) {
        new_field->inner = copy_type(&field->inner);
      }
      field = field->next;

      if (last == NULL) {
        new_type.enumty.fields = new_field;
      } else {
        last->next = new_field;
      }

      last = new_field;
    }

    struct ast_template_ty *template = ty->enumty.templates;
    struct ast_template_ty *last_template = NULL;
    while (template) {
      struct ast_template_ty *new_template = calloc(1, sizeof(struct ast_template_ty));
      *new_template = *template;
      if (template->is_resolved) {
        new_template->resolved = copy_type(&template->resolved);
      }
      template = template->next;

      if (last_template == NULL) {
        new_type.enumty.templates = new_template;
      } else {
        last_template->next = new_template;
      }

      last_template = new_template;
    }
  } else if (ty->ty == AST_TYPE_TEMPLATE) {
    new_type.template.outer = calloc(1, sizeof(struct ast_ty));
    *new_type.template.outer = copy_type(ty->template.outer);

    struct ast_template_ty *inner = ty->template.inners;
    struct ast_template_ty *last_inner = NULL;
    while (inner) {
      struct ast_template_ty *new_inner = calloc(1, sizeof(struct ast_template_ty));
      *new_inner = *inner;
      if (inner->is_resolved) {
        new_inner->resolved = copy_type(&inner->resolved);
      }
      inner = inner->next;

      if (last_inner == NULL) {
        new_type.template.inners = new_inner;
      } else {
        last_inner->next = new_inner;
      }

      last_inner = new_inner;
    }
  }

  return new_type;
}

int type_name_into_as_code(struct ast_ty *ty, char *buf, size_t maxlen) {
  if (!ty) {
    return snprintf(buf, maxlen, "<null-type-ptr>");
  }

  int offset = 0;
  switch (ty->ty) {
    case AST_TYPE_ERROR:
      offset += snprintf(buf, maxlen, "error");
      return offset;
    case AST_TYPE_TBD:
      offset += snprintf(buf, maxlen, "tbd");
      return offset;
    case AST_TYPE_INTEGER:
      if (ty->integer.is_signed) {
        offset += snprintf(buf + offset, maxlen - (size_t)offset, "i");
      } else {
        offset += snprintf(buf + offset, maxlen - (size_t)offset, "u");
      }
      offset += snprintf(buf + offset, maxlen - (size_t)offset, "%zd", ty->integer.width);
      break;
    case AST_TYPE_STRING:
      offset += snprintf(buf, maxlen, "str");
      break;
    case AST_TYPE_FLOAT:
      offset += snprintf(buf, maxlen, "float");
      break;
    case AST_TYPE_FVEC:
      offset += snprintf(buf + offset, maxlen - (size_t)offset, "fvec%zd", ty->fvec.width);
      break;
    case AST_TYPE_VOID:
      offset += snprintf(buf, maxlen, "void");
      break;
    case AST_TYPE_ARRAY: {
      char element_ty[256];
      type_name_into(ty->array.element_ty, element_ty, 256);
      offset +=
          snprintf(buf + offset, maxlen - (size_t)offset, "%s[%zu]", element_ty, ty->array.width);
    } break;
    case AST_TYPE_CUSTOM:
      offset += snprintf(buf, maxlen, "%s", ty->name);
      break;
    case AST_TYPE_STRUCT: {
      offset += snprintf(buf, maxlen, "struct %s { ", ty->name);
      struct ast_struct_field *field = ty->structty.fields;
      while (field) {
        if (!strcmp(field->ty->name, ty->name)) {
          // recursive def
          offset += snprintf(buf + offset, maxlen - (size_t)offset, "struct %s%s; ",
                             field->ty->flags & TYPE_FLAG_PTR ? "*" : "", ty->name);
        } else {
          char field_ty[256];
          type_name_into(field->ty, field_ty, 256);
          offset +=
              snprintf(buf + offset, maxlen - (size_t)offset, "%s %s; ", field_ty, field->name);
        }
        field = field->next;
      }
      offset += snprintf(buf + offset, maxlen - (size_t)offset, "}");
    } break;
    case AST_TYPE_NIL:
      offset += snprintf(buf, maxlen, "nil");
      break;
    case AST_TYPE_TEMPLATE:
      offset += snprintf(buf, maxlen, "template %s", ty->name);
      break;
    case AST_TYPE_ENUM:
      offset += snprintf(buf, maxlen, "enum {\n");
      struct ast_enum_field *field = ty->enumty.fields;
      while (field) {
        offset += snprintf(buf + offset, maxlen - (size_t)offset, "  %s", field->name);
        if (field->has_inner) {
          offset += snprintf(buf + offset, maxlen - (size_t)offset, "(");
          offset += type_name_into(&field->inner, buf + offset, maxlen - (size_t)offset);
          offset += snprintf(buf + offset, maxlen - (size_t)offset, ")");
        }
        if (field->next) {
          offset += snprintf(buf + offset, maxlen - (size_t)offset, ",");
        }
        offset += snprintf(buf + offset, maxlen - (size_t)offset, "\n");
        field = field->next;
      }

      offset += snprintf(buf + offset, maxlen - (size_t)offset, "}");

      break;
    default:
      offset += snprintf(buf, maxlen, "<unknown-type %d>", ty->ty);
      return offset;
  }

  if (ty->flags & TYPE_FLAG_PTR) {
    offset += snprintf(buf + offset, maxlen - (size_t)offset, "*");
  }
  if (ty->flags & TYPE_FLAG_CONSTANT) {
    offset += snprintf(buf + offset, maxlen - (size_t)offset, " const");
  }
  if (ty->flags & ~(TYPE_FLAG_PTR | TYPE_FLAG_CONSTANT)) {
    offset += snprintf(buf + offset, maxlen - (size_t)offset, " (flags %lx)", ty->flags);
  }

  buf[offset] = '\0';
  return offset;
}
