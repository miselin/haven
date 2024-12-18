#include "kv.h"

#include <malloc.h>
#include <string.h>

struct kv_node {
  char *key;
  void *value;
  struct kv_node *next;
};

struct kv {
  struct kv_node *head;
};

struct kv *new_kv(void) {
  struct kv *kv = calloc(1, sizeof(struct kv));
  kv->head = NULL;
  return kv;
}

void kv_insert(struct kv *kv, const char *key, void *value) {
  struct kv_node *node = calloc(1, sizeof(struct kv_node));
  node->key = malloc(strlen(key) + 1);
  strcpy(node->key, key);
  node->value = (void *)value;

  struct kv_node *last = kv->head;
  if (!last) {
    kv->head = node;
    return;
  }

  while (last->next) {
    last = last->next;
  }

  last->next = node;
}

void *kv_lookup(struct kv *kv, const char *key) {
  struct kv_node *cur = kv->head;
  while (cur) {
    if (!strcmp(cur->key, key)) {
      return cur->value;
    }
    cur = cur->next;
  }

  return NULL;
}

void kv_delete(struct kv *kv, const char *key) {
  struct kv_node *cur = kv->head;
  struct kv_node *prev = NULL;
  while (cur) {
    if (!strcmp(cur->key, key)) {
      if (prev) {
        prev->next = cur->next;
      } else {
        kv->head = cur->next;
      }

      free(cur->key);
      free(cur);
      return;
    }

    prev = cur;
    cur = cur->next;
  }
}

void destroy_kv(struct kv *kv) {
  struct kv_node *cur = kv->head;
  while (cur) {
    struct kv_node *next = cur->next;
    free(cur->key);
    free(cur);
    cur = next;
  }

  free(kv);
}

void *kv_iter(struct kv *kv) {
  return kv->head;
}

void *kv_next(void **iter) {
  struct kv_node *node = *iter;
  if (!node) {
    return NULL;
  }

  *iter = node->next;
  return node->value;
}

int kv_end(void *iter) {
  return iter == NULL;
}
