#include "kv.h"

#include <stdlib.h>
#include <string.h>

#include "utility.h"

struct kv_node {
  char *key;
  void *value;
  struct kv_node *next;
};

struct kv {
  struct kv_node *head;
};

extern struct kv *haven_new_kv(void) WEAK;
extern void haven_kv_insert(struct kv *, const char *key, void *value) WEAK;
extern void *haven_kv_lookup(struct kv *, const char *key) WEAK;
extern void *haven_kv_delete(struct kv *, const char *key) WEAK;
extern void haven_destroy_kv(struct kv *trie) WEAK;

extern void *haven_kv_iter(struct kv *kv) WEAK;
extern void *haven_kv_next(void **iter) WEAK;
extern int haven_kv_end(void *iter) WEAK;

struct kv *new_kv(void) {
  if (haven_new_kv) {
    return haven_new_kv();
  }

  struct kv *kv = calloc(1, sizeof(struct kv));
  kv->head = NULL;
  return kv;
}

void kv_insert(struct kv *kv, const char *key, void *value) {
  if (haven_kv_insert) {
    haven_kv_insert(kv, key, value);
    return;
  }

  if (!*key) {
    return;
  }

  struct kv_node *node = calloc(1, sizeof(struct kv_node));
  node->key = malloc(strlen(key) + 1);
  strcpy(node->key, key);
  node->value = (void *)value;

  struct kv_node *last = kv->head;
  struct kv_node *iternode = last;
  if (!last) {
    kv->head = node;
    return;
  }

  while (iternode) {
    last = iternode;

#if 0
    if (!strcmp(iternode->key, key)) {
      iternode->value = value;
      free(node->key);
      free(node);
      return;
    }
#endif

    iternode = iternode->next;
  }

  last->next = node;
}

void *kv_lookup(struct kv *kv, const char *key) {
  if (haven_kv_lookup) {
    void *result = haven_kv_lookup(kv, key);
    return result;
  }

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
  if (haven_kv_delete) {
    haven_kv_delete(kv, key);
    return;
  }

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
  if (haven_destroy_kv) {
    haven_destroy_kv(kv);
    return;
  }

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
  if (haven_kv_iter) {
    return haven_kv_iter(kv);
  }

  return kv->head;
}

void *kv_next(void **iter) {
  if (haven_kv_next) {
    return haven_kv_next(iter);
  }

  struct kv_node *node = *iter;
  if (!node) {
    return NULL;
  }

  *iter = node->next;
  return node->value;
}

int kv_end(void *iter) {
  if (haven_kv_end) {
    return haven_kv_end(iter);
  }

  return iter == NULL;
}
