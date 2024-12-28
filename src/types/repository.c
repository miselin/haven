#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "kv.h"
#include "types.h"

struct ast_ty *copy_type(struct type_repository *repo, struct ast_ty *ty);

static size_t integer_index(size_t width, size_t is_constant);

struct type_repository {
  struct ast_ty signed_integer_types[10];    // i1, i8, i16, i32, i64
  struct ast_ty unsigned_integer_types[10];  // u1, u8, u16, u32, u64
  struct ast_ty float_type;

  struct ast_ty tbd_type;
  struct ast_ty error_type;
  struct ast_ty void_type;

  struct kv *types;

  struct compiler *compiler;
};

struct type_repository_entry {
  struct ast_ty *ty;
  int is_alias;
};

struct type_repository *new_type_repository(struct compiler *compiler) {
  struct type_repository *repo = calloc(1, sizeof(struct type_repository));
  repo->compiler = compiler;
  repo->types = new_kv();

  // i1 / u1
  repo->signed_integer_types[0] =
      (struct ast_ty){.ty = AST_TYPE_INTEGER, .integer = {.is_signed = 1, .width = 1}};
  repo->unsigned_integer_types[0] =
      (struct ast_ty){.ty = AST_TYPE_INTEGER, .integer = {.is_signed = 0, .width = 1}};
  repo->signed_integer_types[5] = (struct ast_ty){
      .ty = AST_TYPE_INTEGER, .integer = {.is_signed = 1, .width = 1}, .flags = TYPE_FLAG_CONSTANT};
  repo->unsigned_integer_types[5] = (struct ast_ty){
      .ty = AST_TYPE_INTEGER, .integer = {.is_signed = 0, .width = 1}, .flags = TYPE_FLAG_CONSTANT};

  // 8, 16, 32, 64 bit signed/unsigned
  for (size_t i = 0; i < 4; ++i) {
    repo->signed_integer_types[i + 1] =
        (struct ast_ty){.ty = AST_TYPE_INTEGER, .integer = {.is_signed = 1, .width = 8U << i}};
    repo->signed_integer_types[i + 1 + 5] =
        (struct ast_ty){.ty = AST_TYPE_INTEGER,
                        .integer = {.is_signed = 1, .width = 8U << i},
                        .flags = TYPE_FLAG_CONSTANT};
    repo->unsigned_integer_types[i + 1] =
        (struct ast_ty){.ty = AST_TYPE_INTEGER, .integer = {.is_signed = 0, .width = 8U << i}};
    repo->unsigned_integer_types[i + 1 + 5] =
        (struct ast_ty){.ty = AST_TYPE_INTEGER,
                        .integer = {.is_signed = 0, .width = 8U << i},
                        .flags = TYPE_FLAG_CONSTANT};
  }

  repo->tbd_type = type_tbd();
  repo->error_type = type_error();
  repo->void_type = type_void();

  repo->float_type = (struct ast_ty){.ty = AST_TYPE_FLOAT};

  return repo;
}

struct ast_ty *type_repository_register(struct type_repository *repo, struct ast_ty *ty) {
  if (type_is_error(ty) || type_is_tbd(ty)) {
    compiler_log(repo->compiler, LogLevelError, "typerepo", "type %s is an error type", ty->name);
    return NULL;
  }

  if (ty->ty == AST_TYPE_CUSTOM) {
    compiler_log(repo->compiler, LogLevelError, "typerepo",
                 "type %s is a custom type, needs to be resolved first", ty->name);
    return NULL;
  }

  char name[1024];
  if (ty->ty == AST_TYPE_INTEGER) {
    size_t index = integer_index(ty->integer.width, ty->flags & TYPE_FLAG_CONSTANT);
    if (index < 10) {
      return ty->integer.is_signed ? &repo->signed_integer_types[index]
                                   : &repo->unsigned_integer_types[index];
    }
  } else if (ty->ty == AST_TYPE_FLOAT) {
    return &repo->float_type;
  } else if (ty->ty == AST_TYPE_VOID) {
    return &repo->void_type;
  }

  type_name_into(ty, name, 1024);

  // do we need to add it?
  if (kv_lookup(repo->types, name)) {
    compiler_log(repo->compiler, LogLevelError, "typerepo", "type %s already registered", name);
    return NULL;
  }

  struct type_repository_entry *entry = calloc(1, sizeof(struct type_repository_entry));
  entry->ty = copy_type(repo, ty);
  entry->is_alias = 0;

  compiler_log(repo->compiler, LogLevelDebug, "typerepo", "registering type %s", name);
  kv_insert(repo->types, name, entry);

  return entry->ty;
}

struct ast_ty *type_repository_register_alias(struct type_repository *repo, const char *name,
                                              struct ast_ty *ty) {
  if (type_is_error(ty) || type_is_tbd(ty)) {
    compiler_log(repo->compiler, LogLevelError, "typerepo",
                 "type %s is an error type, won't register alias %s", ty->name, name);
  }

  struct type_repository_entry *entry = kv_lookup(repo->types, name);
  if (entry) {
    compiler_log(repo->compiler, LogLevelError, "typerepo", "alias %s already registered", name);
    return NULL;
  }

  struct ast_ty *target = type_repository_lookup_ty(repo, ty);
  if (!target) {
    compiler_log(repo->compiler, LogLevelError, "typerepo", "alias %s references unknown type %s",
                 name, ty->name);
    return NULL;
  }

  entry = calloc(1, sizeof(struct type_repository_entry));
  entry->ty = target;
  entry->is_alias = 1;

  compiler_log(repo->compiler, LogLevelDebug, "typerepo", "registering alias %s", name);
  kv_insert(repo->types, name, entry);

  return target;
}

struct ast_ty *type_repository_lookup(struct type_repository *repo, const char *name) {
  struct type_repository_entry *entry = kv_lookup(repo->types, name);
  return entry ? entry->ty : NULL;
}

struct ast_ty *type_repository_lookup_ty(struct type_repository *repo, struct ast_ty *ty) {
  if (ty->ty == AST_TYPE_CUSTOM) {
    return type_repository_lookup(repo, ty->name);
  }

  if (ty->ty == AST_TYPE_INTEGER) {
    size_t index = integer_index(ty->integer.width, ty->flags & TYPE_FLAG_CONSTANT);
    if (index < 10) {
      return ty->integer.is_signed ? &repo->signed_integer_types[index]
                                   : &repo->unsigned_integer_types[index];
    }
  } else if (ty->ty == AST_TYPE_FLOAT) {
    return &repo->float_type;
  } else if (ty->ty == AST_TYPE_VOID) {
    return &repo->void_type;
  } else if (ty->ty == AST_TYPE_ERROR) {
    return &repo->error_type;
  } else if (ty->ty == AST_TYPE_TBD) {
    return &repo->tbd_type;
  }

  char name[1024];
  type_name_into(ty, name, 1024);

  struct type_repository_entry *entry = kv_lookup(repo->types, name);
  compiler_log(repo->compiler, LogLevelError, "typerepo", "lookup_ty looking up %s, got %p", name,
               (void *)entry);
  return entry ? entry->ty : NULL;
}

struct ast_ty *type_repository_tbd(struct type_repository *repo) {
  return &repo->tbd_type;
}

struct ast_ty *type_repository_void(struct type_repository *repo) {
  return &repo->void_type;
}

struct ast_ty *type_repository_error(struct type_repository *repo) {
  return &repo->error_type;
}

int type_repository_is_shared_type(struct type_repository *repo, struct ast_ty *ty) {
  if (ty == &repo->tbd_type || ty == &repo->error_type || ty == &repo->void_type) {
    return 1;
  }

  if (ty->ty == AST_TYPE_INTEGER) {
    for (size_t i = 0; i < 10; ++i) {
      if (ty == &repo->signed_integer_types[i] || ty == &repo->unsigned_integer_types[i]) {
        return 1;
      }
    }
  } else if (ty->ty == AST_TYPE_FLOAT) {
    return ty == &repo->float_type;
  }

  return 0;
}

static void type_repository_free(struct type_repository *repo,
                                 struct type_repository_entry *entry) {
  if (!entry->is_alias) {
    free_ty(repo->compiler, entry->ty, 1);
  }

  free(entry);
}

void type_repostory_remove(struct type_repository *repo, const char *name) {
  struct type_repository_entry *entry = kv_lookup(repo->types, name);
  if (entry) {
    kv_delete(repo->types, name);
    type_repository_free(repo, entry);
  }
}

void destroy_type_repository(struct type_repository *repo) {
  void *iter = kv_iter(repo->types);
  while (iter) {
    struct type_repository_entry *entry = kv_next(&iter);
    type_repository_free(repo, entry);
  }
  destroy_kv(repo->types);
  free(repo);
}

static size_t integer_index(size_t width, size_t is_constant) {
  size_t base = is_constant ? 5 : 0;

  switch (width) {
    case 1:
      return 0 + base;
    case 8:
      return 1 + base;
    case 16:
      return 2 + base;
    case 32:
      return 3 + base;
    case 64:
      return 4 + base;
    default:
      return ~0U;
  }
}
