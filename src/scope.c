#include "scope.h"

#include <malloc.h>
#include <stdio.h>

#include "kv.h"

struct scope {
  struct kv *values;
  struct scope *parent;
};

struct scope *enter_scope(struct scope *parent) {
  struct scope *result = calloc(1, sizeof(struct scope));
  result->values = new_kv();
  result->parent = parent;
  return result;
}

struct scope *exit_scope(struct scope *scope) {
  struct scope *parent = scope->parent;
  destroy_kv(scope->values);
  free(scope);
  return parent;
}

void scope_insert(struct scope *scope, const char *name, void *value) {
  kv_delete(scope->values, name);
  kv_insert(scope->values, name, value);
}

void *scope_lookup(struct scope *scope, const char *name, int recurse) {
  void *result = kv_lookup(scope->values, name);
  if (result) {
    return result;
  }

  if (recurse && scope->parent) {
    return scope_lookup(scope->parent, name, recurse);
  }

  return NULL;
}

struct scope *scope_parent(struct scope *scope) {
  return scope->parent;
}
