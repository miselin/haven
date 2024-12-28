#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "kv.h"
#include "types.h"

struct ast_ty *copy_type(struct type_repository *repo, struct ast_ty *ty);

struct type_repository {
  struct ast_ty signed_integer_types[4];    // i8, i16, i32, i64
  struct ast_ty unsigned_integer_types[4];  // u8, u16, u32, u64
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

  for (size_t i = 0; i < 4; ++i) {
    repo->signed_integer_types[i] =
        (struct ast_ty){.ty = AST_TYPE_INTEGER, .integer = {.is_signed = 1, .width = 8U << i}};
    repo->unsigned_integer_types[i] =
        (struct ast_ty){.ty = AST_TYPE_INTEGER, .integer = {.is_signed = 0, .width = 8U << i}};
  }

  repo->tbd_type = type_tbd();
  repo->error_type = type_error();
  repo->void_type = type_void();

  repo->float_type = (struct ast_ty){.ty = AST_TYPE_FLOAT};

  return repo;
}

struct ast_ty *type_repository_register(struct type_repository *repo, struct ast_ty *ty) {
  if (type_is_error(ty) || type_is_tbd(ty)) {
    compiler_log(repo->compiler, LogLevelError, "type_repository", "type %s is an error type",
                 ty->name);
    return NULL;
  }

  if (ty->ty == AST_TYPE_CUSTOM) {
    compiler_log(repo->compiler, LogLevelError, "type_repository",
                 "type %s is a custom type, needs to be resolved first", ty->name);
    return NULL;
  }

  char name[1024];
  if (ty->ty == AST_TYPE_INTEGER) {
    // not an error to re-register these
    if (ty->integer.width == 8 || ty->integer.width == 16 || ty->integer.width == 32 ||
        ty->integer.width == 64) {
      return &repo->signed_integer_types[(ty->integer.width / 8) - 1];
    }
  } else if (ty->ty == AST_TYPE_FLOAT) {
    return &repo->float_type;
  } else if (ty->ty == AST_TYPE_VOID) {
    return &repo->void_type;
  }

  type_name_into(ty, name, 1024);

  // do we need to add it?
  if (kv_lookup(repo->types, name)) {
    compiler_log(repo->compiler, LogLevelError, "type_repository", "type %s already registered",
                 name);
    return NULL;
  }

  struct type_repository_entry *entry = calloc(1, sizeof(struct type_repository_entry));
  entry->ty = copy_type(repo, ty);
  entry->is_alias = 0;

  compiler_log(repo->compiler, LogLevelDebug, "type_repository", "registering type %s", name);
  kv_insert(repo->types, name, entry);

  return entry->ty;
}

struct ast_ty *type_repository_register_alias(struct type_repository *repo, const char *name,
                                              struct ast_ty *ty) {
  if (type_is_error(ty) || type_is_tbd(ty)) {
    compiler_log(repo->compiler, LogLevelError, "type_repository",
                 "type %s is an error type, won't register alias %s", ty->name, name);
  }

  struct type_repository_entry *entry = kv_lookup(repo->types, name);
  if (entry) {
    compiler_log(repo->compiler, LogLevelError, "type_repository", "alias %s already registered",
                 name);
    return NULL;
  }

  struct ast_ty *target = type_repository_lookup_ty(repo, ty);
  if (!target) {
    compiler_log(repo->compiler, LogLevelError, "type_repository",
                 "alias %s references unknown type %s", name, ty->name);
    return NULL;
  }

  entry = calloc(1, sizeof(struct type_repository_entry));
  entry->ty = target;
  entry->is_alias = 1;

  compiler_log(repo->compiler, LogLevelDebug, "type_repository", "registering alias %s", name);
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
    for (size_t i = 0; i < 4; ++i) {
      if (repo->signed_integer_types[i].integer.width == ty->integer.width) {
        return &repo->signed_integer_types[i];
      }
      if (repo->unsigned_integer_types[i].integer.width == ty->integer.width) {
        return &repo->unsigned_integer_types[i];
      }
    }
  } else if (ty->ty == AST_TYPE_FLOAT) {
    return &repo->float_type;
  }

  char name[1024];
  type_name_into(ty, name, 1024);

  struct type_repository_entry *entry = kv_lookup(repo->types, name);
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
    for (size_t i = 0; i < 4; ++i) {
      if (ty == &repo->signed_integer_types[i] || ty == &repo->unsigned_integer_types[i]) {
        return 1;
      }
    }
  } else if (ty->ty == AST_TYPE_FLOAT) {
    return ty == &repo->float_type;
  }

  return 0;
}

void type_repostory_remove(struct type_repository *repo, const char *name) {
  struct type_repository_entry *entry = kv_lookup(repo->types, name);
  if (entry) {
    kv_delete(repo->types, name);
    if (!entry->is_alias) {
      free_ty(repo->compiler, entry->ty, 1);
    }
    free(entry);
  }
}

void destroy_type_repository(struct type_repository *repo) {
  void *iter = kv_iter(repo->types);
  while (iter) {
    struct type_repository_entry *entry = kv_next(&iter);
    if (!entry->is_alias) {
      free_ty(repo->compiler, entry->ty, 1);
    }
    free(entry);
  }
  destroy_kv(repo->types);
  free(repo);
}
