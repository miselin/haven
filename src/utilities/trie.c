#include <malloc.h>

struct trie_node {
  char c;
  void *value;
  struct trie_node *left;
  struct trie_node *right;
};

struct trie {
  struct trie_node *root;
};

struct trie *new_trie(void) {
  struct trie *trie = calloc(1, sizeof(struct trie));
  trie->root = calloc(1, sizeof(struct trie_node));
  return trie;
}

void trie_insert(struct trie *trie, const char *key, void *value) {
  struct trie_node *node = trie->root;
  while (*key) {
    if (*key == node->c) {
      key++;
      continue;
    }

    if (*key < node->c) {
      if (node->left) {
        node = node->left;
      } else {
        struct trie_node *new_node = calloc(1, sizeof(struct trie_node));
        new_node->c = *key;
        node->left = new_node;
        node = new_node;
      }
    } else {
      if (node->right) {
        node = node->right;
      } else {
        struct trie_node *new_node = calloc(1, sizeof(struct trie_node));
        new_node->c = *key;
        node->right = new_node;
        node = new_node;
      }
    }
  }

  node->value = value;
}

void *trie_lookup(struct trie *trie, const char *key) {
  struct trie_node *node = trie->root;
  while (*key) {
    if (*key == node->c) {
      key++;
      continue;
    }

    if (*key < node->c) {
      if (node->left) {
        node = node->left;
      } else {
        return NULL;
      }
    } else {
      if (node->right) {
        node = node->right;
      } else {
        return NULL;
      }
    }
  }

  return node->value;
}

static void destroy_trie_node(struct trie_node *node) {
  if (!node) {
    return;
  }

  destroy_trie_node(node->left);
  destroy_trie_node(node->right);
  free(node);
}

void destroy_trie(struct trie *trie) {
  destroy_trie_node(trie->root);
  free(trie);
}