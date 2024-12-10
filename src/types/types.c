#include "types.h"

#include <string.h>

#include "typecheck.h"

struct ast_ty type_tbd() {
  struct ast_ty ty;
  memset(&ty, 0, sizeof(ty));
  ty.ty = AST_TYPE_TBD;
  return ty;
}

struct ast_ty type_void() {
  struct ast_ty ty;
  memset(&ty, 0, sizeof(ty));
  ty.ty = AST_TYPE_VOID;
  return ty;
}

struct ast_ty type_error() {
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

int same_type_class(struct ast_ty *ty1, struct ast_ty *ty2) {
  int same = (ty1->ty == ty2->ty) && (ty1->flags == ty2->flags);
  if (ty1->ty == AST_TYPE_ARRAY) {
    same = same && same_type_class(ty1->array.element_ty, ty2->array.element_ty);
  }
  return same;
}

int same_type(struct ast_ty *ty1, struct ast_ty *ty2) {
  if (!same_type_class(ty1, ty2)) {
    return 0;
  }

  switch (ty1->ty) {
    case AST_TYPE_INTEGER:
      return ty1->integer.is_signed == ty2->integer.is_signed &&
             ty1->integer.width == ty2->integer.width;
    case AST_TYPE_FVEC:
      return ty1->fvec.width == ty2->fvec.width;
    case AST_TYPE_ARRAY:
      return ty1->array.width == ty2->array.width &&
             same_type(ty1->array.element_ty, ty2->array.element_ty);
    default:
      return 1;
  }
}

int narrower_type(struct ast_ty *ty1, struct ast_ty *ty2) {
  if (!same_type_class(ty1, ty2)) {
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
  if (!same_type_class(ty1, ty2)) {
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

void type_name_into(struct ast_ty *ty, char *buf, size_t maxlen) {
  int offset = 0;
  switch (ty->ty) {
    case AST_TYPE_ERROR:
      offset += snprintf(buf, maxlen, "error");
      return;
    case AST_TYPE_TBD:
      offset += snprintf(buf, maxlen, "tbd");
      return;
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
    case AST_TYPE_CHAR:
      offset += snprintf(buf, maxlen, "char");
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
    default:
      snprintf(buf, maxlen, "<unknown-type %d>", ty->ty);
      return;
  }

  if (ty->flags & TYPE_FLAG_PTR) {
    offset += snprintf(buf + offset, maxlen - (size_t)offset, "*");
  }

  buf[offset] = '\0';
}

int can_cast(struct ast_ty *ty1, struct ast_ty *ty2) {
  // identical types?
  if (same_type(ty1, ty2)) {
    return 1;
  } else if (same_type_class(ty1, ty2)) {
    return 1;
  }

  // float <> int
  if ((ty1->ty == AST_TYPE_INTEGER && ty2->ty == AST_TYPE_FLOAT) ||
      (ty1->ty == AST_TYPE_FLOAT && ty2->ty == AST_TYPE_INTEGER)) {
    return 1;
  }

  return 0;
}
