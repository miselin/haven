#include "types.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "utility.h"

struct type_name_context {
  struct ast_ty *ty;
  struct type_name_context *next;
};

static int type_name_into_ctx(struct ast_ty *ty, struct string_builder *builder,
                              struct type_name_context *ctx);

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
  if (ty->ty == AST_TYPE_POINTER || ty->ty == AST_TYPE_BOX) {
    return 0;
  }

  if (ty->ty == AST_TYPE_STRUCT) {
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
    if (otherty->ty != AST_TYPE_POINTER) {
      return 0;
    }

    struct ast_ty *pointee = ptr_pointee_type(otherty);
    if (pointee->ty == AST_TYPE_INTEGER && pointee->integer.width == 8) {
      return 1;
    }
  }

  if (ty1->ty == AST_TYPE_POINTER || ty2->ty == AST_TYPE_POINTER) {
    struct ast_ty *ptrty = ty1->ty == AST_TYPE_POINTER ? ty1 : ty2;
    struct ast_ty *otherty = ty1->ty == AST_TYPE_POINTER ? ty2 : ty1;

    if (otherty->ty == AST_TYPE_ARRAY) {
      return same_type_class(ptr_pointee_type(ptrty), otherty->array.element_ty, flagmask);
    }
  }

  return same;
}

int compatible_types(struct ast_ty *ty1, struct ast_ty *ty2) {
  if (same_type(ty1, ty2)) {
    return 1;
  }

  // pointers can be converted to each other
  if (ty1->ty == AST_TYPE_POINTER && ty2->ty == AST_TYPE_POINTER) {
    // TODO: only if underlying types are the same
    return 1;
  }

  // arrays can be pointers
  if ((ty1->ty == AST_TYPE_POINTER && ty2->ty == AST_TYPE_ARRAY) ||
      (ty1->ty == AST_TYPE_ARRAY && ty2->ty == AST_TYPE_POINTER)) {
    // TODO: only if underlying types are the same
    return 1;
  }

  // integers can become pointers
  if (ty1->ty == AST_TYPE_INTEGER && ty2->ty == AST_TYPE_POINTER) {
    return 1;
  } else if (ty1->ty == AST_TYPE_POINTER && ty2->ty == AST_TYPE_INTEGER) {
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

  // same_type_class said this mismatch was fine, but all other checks after this
  // require the types to truly be the same (or else they'll compare the wrong things)
  if (ty1->ty != ty2->ty) {
    return 1;
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
  if (type_is_nil(ty1) || type_is_nil(ty2)) {
    // nil is magic
    return 1;
  }

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

struct ast_ty ptr_type(struct ast_ty pointee) {
  struct ast_ty ty;
  memset(&ty, 0, sizeof(ty));
  ty.ty = AST_TYPE_POINTER;
  ty.pointer.pointee = calloc(1, sizeof(struct ast_ty));
  *ty.pointer.pointee = pointee;
  return ty;
}

struct ast_ty *ptr_pointee_type(struct ast_ty *ty) {
  if (ty->ty != AST_TYPE_POINTER) {
    return NULL;
  }

  return ty->pointer.pointee;
}

struct ast_ty box_type(struct ast_ty pointee) {
  struct ast_ty ty;
  memset(&ty, 0, sizeof(ty));
  ty.ty = AST_TYPE_BOX;
  ty.pointer.pointee = calloc(1, sizeof(struct ast_ty));
  *ty.pointer.pointee = pointee;
  return ty;
}

struct ast_ty *box_pointee_type(struct ast_ty *ty) {
  if (ty->ty != AST_TYPE_BOX) {
    return NULL;
  }

  return ty->pointer.pointee;
}

const char *type_name(struct ast_ty *ty) {
  static char buf[256];
  type_name_into(ty, buf, 256);
  return buf;
}

int type_name_into(struct ast_ty *ty, char *buf, size_t maxlen) {
  struct string_builder *builder = new_string_builder_for(buf, maxlen);
  type_name_into_ctx(ty, builder, NULL);

  size_t len = string_builder_len(builder);
  int wanted_resize = string_builder_needs_resize(builder);
  free_string_builder(builder);

  buf[len] = 0;
  return wanted_resize ? -1 : 0;
}

static int type_name_into_ctx(struct ast_ty *ty, struct string_builder *builder,
                              struct type_name_context *ctx) {
  if (!ty) {
    string_builder_append(builder, "<null-type-ptr>");
    return 0;
  }

  int offset = 0;
  switch (ty->ty) {
    case AST_TYPE_ERROR:
      string_builder_append(builder, "error");
      return offset;

    case AST_TYPE_TBD:
      string_builder_append(builder, "tbd");
      return offset;

    case AST_TYPE_INTEGER:
      string_builder_appendf(builder, "%c%zd", ty->integer.is_signed ? 'i' : 'u',
                             ty->integer.width);
      break;

    case AST_TYPE_STRING:
      string_builder_append(builder, "str");
      break;

    case AST_TYPE_FLOAT:
      string_builder_append(builder, "float");
      break;

    case AST_TYPE_FVEC:
      string_builder_appendf(builder, "fvec%zd", ty->fvec.width);
      break;

    case AST_TYPE_VOID:
      string_builder_append(builder, "void");
      break;

    case AST_TYPE_ARRAY: {
      type_name_into_ctx(ty->array.element_ty, builder, ctx);
      string_builder_appendf(builder, "[%zu]", ty->array.width);
    } break;

    case AST_TYPE_CUSTOM:
      string_builder_appendf(builder, "Ty(%s)", ty->name);
      if (ty->custom.is_forward_decl) {
        string_builder_append(builder, " (forward)");
      }
      break;

    case AST_TYPE_STRUCT: {
      // did we already see this struct earlier in the chain?
      struct type_name_context *seen = ctx;
      while (seen) {
        if (!strcmp(ty->name, seen->ty->name)) {
          break;
        } else if (seen->ty == ty) {
          break;
        }
        seen = seen->next;
      }

      if (seen) {
        string_builder_appendf(builder, "%s %s", ty->structty.is_union ? "union" : "struct",
                               ty->name);
      } else {
        struct type_name_context *new_ctx = malloc(sizeof(struct type_name_context));
        new_ctx->ty = ty;
        new_ctx->next = ctx;

        string_builder_appendf(builder, "%s %s { ", ty->structty.is_union ? "union" : "struct",
                               ty->name);
        struct ast_struct_field *field = ty->structty.fields;
        while (field) {
          struct ast_ty *field_ty = field->ty ? field->ty : &field->parsed_ty;
          if (field_ty->ty == AST_TYPE_POINTER || field_ty->ty == AST_TYPE_BOX) {
            struct ast_ty *pointee = field_ty->pointer.pointee;

            // don't emit the pointee type, just the name will do
            string_builder_appendf(builder, "struct %s %s%s; ",
                                   pointee ? pointee->name : "<null-pointee>",
                                   field_ty->ty == AST_TYPE_POINTER ? "* " : "^ ", field->name);
          } else {
            type_name_into_ctx(field_ty, builder, new_ctx);
            string_builder_appendf(builder, " %s; ", field->name);
          }
          field = field->next;
        }
        string_builder_append(builder, "}");

        free(new_ctx);
      }
    } break;

    case AST_TYPE_NIL:
      string_builder_append(builder, "nil");
      break;

    case AST_TYPE_TEMPLATE:
      string_builder_append(builder, "template ");
      type_name_into_ctx(ty->tmpl.outer, builder, ctx);
      string_builder_append(builder, "<");
      struct ast_template_ty *inner = ty->tmpl.inners;
      while (inner) {
        string_builder_appendf(builder, "%s %d -> ", inner->name, inner->is_resolved);
        if (inner->resolved) {
          type_name_into_ctx(inner->resolved, builder, ctx);
        }
        if (inner->next) {
          string_builder_append(builder, ", ");
        }
        inner = inner->next;
      }
      string_builder_append(builder, ">");
      break;

    case AST_TYPE_ENUM:
      string_builder_appendf(builder, "enum %s <", ty->name);
      struct ast_template_ty *template = ty->enumty.templates;
      while (template) {
        string_builder_appendf(builder, "%s (%s)", template->name,
                               template->is_resolved ? "resolved" : "unresolved");
        if (template->next) {
          string_builder_append(builder, ", ");
        }
        template = template->next;
      }

      string_builder_append(builder, "> { ");
      struct ast_enum_field *field = ty->enumty.fields;
      while (field) {
        string_builder_appendf(builder, "%s = %" PRIu64, field->name, field->value);
        if (field->has_inner) {
          struct ast_ty *enum_inner = field->inner ? field->inner : &field->parser_inner;
          string_builder_append(builder, " (");
          type_name_into_ctx(enum_inner, builder, ctx);
          string_builder_append(builder, ")");
        }
        if (field->next) {
          string_builder_append(builder, ", ");
        }
        field = field->next;
      }

      string_builder_append(builder, "}");
      break;

    case AST_TYPE_FUNCTION:
      string_builder_append(builder, "fn (");
      for (size_t i = 0; i < ty->function.num_params; i++) {
        type_name_into_ctx(ty->function.param_types[i], builder, ctx);
        if (i + 1 < ty->function.num_params) {
          string_builder_append(builder, ", ");
        }
      }
      string_builder_append(builder, ") -> ");
      type_name_into_ctx(ty->function.retty, builder, ctx);
      break;

    case AST_TYPE_MATRIX:
      string_builder_appendf(builder, "matrix %zdx%zd", ty->matrix.cols, ty->matrix.rows);
      break;

    case AST_TYPE_POINTER:
      string_builder_append(builder, "Pointer <");
      type_name_into_ctx(ty->pointer.pointee, builder, ctx);
      string_builder_append(builder, ">");
      break;

    case AST_TYPE_BOX:
      string_builder_append(builder, "Box <");
      type_name_into_ctx(ty->pointer.pointee, builder, ctx);
      string_builder_append(builder, ">");
      break;

    default:
      string_builder_appendf(builder, "<unknown-type %d>", ty->ty);
  }

  if (ty->flags & TYPE_FLAG_CONSTANT) {
    string_builder_append(builder, " const");
  }
  if (ty->flags & ~TYPE_FLAG_CONSTANT) {
    string_builder_appendf(builder, " (flags %" PRIx64 ")", ty->flags);
  }

  return 0;
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
  if (ty->ty == AST_TYPE_POINTER || ty->ty == AST_TYPE_BOX) {
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
      size_t largest_field = 0;
      struct ast_struct_field *field = ty->structty.fields;
      while (field) {
        size_t field_size = type_size(field->ty);
        size += field_size;
        if (field_size > largest_field) {
          largest_field = field_size;
        }
        field = field->next;
      }
      return ty->structty.is_union ? largest_field : size;
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
          size_t tysz = 0;  // TODO type_size(&field->inner);
          if (tysz > largest_inner) {
            largest_inner = tysz;
          }
        }
        field = field->next;
      }

      size += largest_inner;

      return size;
    } break;
    case AST_TYPE_MATRIX:
      return ty->matrix.cols * ty->matrix.rows * 4;
      break;

    case AST_TYPE_CUSTOM:
      // unresolved, emit the smallest possible storage for it
      return 1;

    default:
      fprintf(stderr, "type_size unhandled %d [%s]\n", ty->ty, ty->name);
      return 0;
  }
}

struct ast_ty *copy_type(struct type_repository *repo, struct ast_ty *ty) {
  struct ast_ty *new_type = calloc(1, sizeof(struct ast_ty));
  *new_type = *ty;

  if (ty->specialization_of) {
    new_type->specialization_of = strdup(ty->specialization_of);
  }

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
      new_type->array.element_ty = type_repository_lookup_ty(repo, ty->array.element_ty);
      break;

    case AST_TYPE_STRUCT: {
      new_type->structty.fields = NULL;
      struct ast_struct_field *field = ty->structty.fields;
      struct ast_struct_field *last = NULL;
      while (field) {
        struct ast_struct_field *new_field = calloc(1, sizeof(struct ast_struct_field));
        *new_field = *field;
        new_field->ty = type_repository_lookup_ty(repo, field->ty ? field->ty : &field->parsed_ty);
        field = field->next;

        if (last == NULL) {
          new_type->structty.fields = new_field;
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
          new_field->inner =
              type_repository_lookup_ty(repo, field->inner ? field->inner : &field->parser_inner);
        }
        field = field->next;

        if (last == NULL) {
          new_type->enumty.fields = new_field;
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
          new_template->resolved = type_repository_lookup_ty(repo, template->resolved);
        }
        template = template->next;

        if (last_template == NULL) {
          new_type->enumty.templates = new_template;
        } else {
          last_template->next = new_template;
        }

        last_template = new_template;
      }
    } break;

    case AST_TYPE_TEMPLATE: {
      new_type->tmpl.outer = type_repository_lookup_ty(repo, ty->tmpl.outer);

      struct ast_template_ty *inner = ty->tmpl.inners;
      struct ast_template_ty *last_inner = NULL;
      while (inner) {
        struct ast_template_ty *new_inner = calloc(1, sizeof(struct ast_template_ty));
        *new_inner = *inner;
        if (inner->is_resolved) {
          new_inner->resolved = type_repository_lookup_ty(repo, inner->resolved);
        }
        inner = inner->next;

        if (last_inner == NULL) {
          new_type->tmpl.inners = new_inner;
        } else {
          last_inner->next = new_inner;
        }

        last_inner = new_inner;
      }
    } break;

    case AST_TYPE_FUNCTION: {
      new_type->function.param_types = calloc(ty->function.num_params, sizeof(struct ast_ty));
      for (size_t i = 0; i < ty->function.num_params; i++) {
        new_type->function.param_types[i] =
            type_repository_lookup_ty(repo, ty->function.param_types[i]);
      }

      new_type->function.retty = type_repository_lookup_ty(repo, ty->function.retty);
    } break;

    case AST_TYPE_POINTER:
    case AST_TYPE_BOX: {
      new_type->pointer.pointee = type_repository_lookup_ty(repo, ty->pointer.pointee);
      if (!new_type->pointer.pointee) {
        // make it a custom instead and let it get resolved later
        struct ast_ty custom;
        custom.ty = AST_TYPE_CUSTOM;
        strncpy(custom.name, ty->pointer.pointee->name, sizeof(custom.name));
        new_type->pointer.pointee = type_repository_lookup_ty(repo, &custom);
      }
    } break;
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
      type_name_into_as_code(ty->array.element_ty, element_ty, 256);
      offset +=
          snprintf(buf + offset, maxlen - (size_t)offset, "%s[%zu]", element_ty, ty->array.width);
    } break;

    case AST_TYPE_CUSTOM:
      offset += snprintf(buf, maxlen, "%s", ty->name);
      break;

    case AST_TYPE_STRUCT: {
      offset += snprintf(buf, maxlen, "%s %s%s{ ", ty->structty.is_union ? "union" : "struct",
                         ty->name, ty->name[0] ? " " : "");
      struct ast_struct_field *field = ty->structty.fields;
      while (field) {
        struct ast_ty *field_ty = field->ty ? field->ty : &field->parsed_ty;
        char field_tyname[256];
        type_name_into_as_code(field_ty, field_tyname, 256);
        offset +=
            snprintf(buf + offset, maxlen - (size_t)offset, "%s %s; ", field_tyname, field->name);
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
          offset += type_name_into_as_code(field->inner ? field->inner : &field->parser_inner,
                                           buf + offset, maxlen - (size_t)offset);
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

    case AST_TYPE_POINTER:
      offset += type_name_into_as_code(ty->pointer.pointee, buf + offset, maxlen - (size_t)offset);
      offset += snprintf(buf + offset, maxlen - (size_t)offset, "*");
      break;

    default:
      offset += snprintf(buf, maxlen, "<unknown-type %d>", ty->ty);
      return offset;
  }

  buf[offset] = '\0';
  return offset;
}

int type_is_indexable(struct ast_ty *ty) {
  return ty->ty == AST_TYPE_ARRAY || ty->ty == AST_TYPE_POINTER || ty->ty == AST_TYPE_FVEC ||
         ty->ty == AST_TYPE_MATRIX || ty->ty == AST_TYPE_BOX;
}

void mangle_type(struct ast_ty *ty, char *buf, size_t len, const char *prefix) {
  *buf = 0;

  if (prefix) {
    strcat(buf, prefix);
    size_t prefix_len = strlen(prefix);
    buf += prefix_len;
    len -= prefix_len;
  }

  switch (ty->ty) {
    case AST_TYPE_INTEGER:
      snprintf(buf, len, "i%zd", ty->integer.width);
      break;

    case AST_TYPE_STRING:
      strcat(buf, "s");
      break;

    case AST_TYPE_FLOAT:
      strcat(buf, "F32");
      break;

    case AST_TYPE_FVEC:
      snprintf(buf, len, "F32V%zd", ty->fvec.width);
      break;

    case AST_TYPE_VOID:
      strcat(buf, "V");
      break;

    case AST_TYPE_ARRAY:
      snprintf(buf, len, "A%zd", ty->array.width);
      break;

    case AST_TYPE_MATRIX:
      snprintf(buf, len, "M%zdx%zd", ty->matrix.rows, ty->matrix.cols);
      break;

    case AST_TYPE_STRUCT:
      snprintf(buf, len, "S%s", ty->name);
      break;

    case AST_TYPE_ENUM:
      snprintf(buf, len, "E%s", ty->name);
      break;

    case AST_TYPE_CUSTOM:
      snprintf(buf, len, "C%s", ty->name);
      break;

    case AST_TYPE_NIL:
      strcat(buf, "N");
      break;

    case AST_TYPE_FUNCTION:
      strcat(buf, "Fn");
      break;

    case AST_TYPE_POINTER:
      strcat(buf, "P");
      break;

    case AST_TYPE_BOX:
      strcat(buf, "B");
      break;

    case AST_TYPE_TEMPLATE:
      strcat(buf, "T");
      strcat(buf, ty->tmpl.outer->name);
      strcat(buf, "_");
      struct ast_template_ty *inner = ty->tmpl.inners;
      while (inner) {
        char inner_buf[256];
        mangle_type(&inner->parsed_ty, inner_buf, 256, NULL);
        strcat(buf, inner_buf);
        if (inner->next) {
          strcat(buf, "_");
        }
        inner = inner->next;
      }
      break;

    case AST_TYPE_ERROR:
    case AST_TYPE_TBD:
      // no.
      break;
  }
}
