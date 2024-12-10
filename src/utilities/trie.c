#include <llvm-c-18/llvm-c/Types.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>

struct trie_node {
  size_t id;
  char key[256];
  int has_value;
  void *value;

  // TODO: this isn't space efficient, but it's easier to implement and still fast
  struct trie_node *children[256];
};

struct trie {
  struct trie_node *root;
  size_t next_id;
};

struct trie *new_trie(void) {
  struct trie *trie = calloc(1, sizeof(struct trie));
  trie->root = calloc(1, sizeof(struct trie_node));
  return trie;
}

static int has_children(struct trie_node *node) {
  if (!node) {
    return 0;
  }

  for (size_t i = 0; i < 256; ++i) {
    if (node->children[i]) {
      return 1;
    }
  }

  return 0;
}

void trie_insert(struct trie *trie, const char *key, void *value) {
  if (!*key) {
    // no-op
    return;
  }

  struct trie_node *node = trie->root;
  // root node is special, we don't have a key on it
  struct trie_node *root_child = node->children[(int)*key];
  if (!root_child) {
    root_child = calloc(1, sizeof(struct trie_node));
    root_child->id = trie->next_id++;
    root_child->key[0] = *key;
    node->children[(int)*key] = root_child;
  }

  node = root_child;

  size_t i = 0;
  while (*key) {
    if (*key == node->key[i]) {
      key++;
      i++;
    }

    if (node->key[i]) {
      // split the node, partial match
      struct trie_node *split_node = calloc(1, sizeof(struct trie_node));
      split_node->id = trie->next_id++;
      strcpy(split_node->key, node->key + i);

      split_node->has_value = node->has_value;
      split_node->value = node->value;

      // old node now gets the prefix
      node->key[i] = 0;
      node->has_value = 0;

      // ... and the split node is now a child of the old node
      node->children[(int)split_node->key[0]] = split_node;
    }

    if (!*key) {
      // needed to split but now we're done, the split node is now the leaf
      break;
    }

    // reached end of the node's key, do we have any children?
    if (!has_children(node)) {
      // no children: just append the key and finish inserting
      while (*key) {
        node->key[i++] = *key++;
      }
      node->key[i++] = 0;
      break;
    }

    struct trie_node *child = node->children[(int)*key];
    if (!child) {
      // no child, create one
      // we can use the full key here, there's no other children on this character
      child = calloc(1, sizeof(struct trie_node));
      child->id = trie->next_id++;
      strcpy(child->key, key);
      node->children[(int)*key] = child;

      node = child;
      break;
    }

    node = child;
    i = 0;
  }

  node->has_value = 1;
  node->value = value;
}

void *trie_lookup(struct trie *trie, const char *key) {
  struct trie_node *node = trie->root;
  size_t i = 0;
  while (node && *key) {
    if (*key == node->key[i]) {
      key++;
      i++;
      continue;
    }

    node = node->children[(int)*key];
    i = 0;
  }

  if (!node) {
    return NULL;
  } else if (*key || node->key[i]) {
    // key wasn't totally consumed, or the node key wasn't totally consumed
    // either way, not a match!
    return NULL;
  }

  return node->has_value ? node->value : NULL;
}

static void dump_trie_node(FILE *stream, struct trie_node *node) {
  if (!node) {
    return;
  }

  fprintf(stream, "  %zd [label=\"%s\nhas_value=%d\"];\n", node->id, node->key, node->has_value);
  for (size_t i = 0; i < 256; ++i) {
    if (node->children[i]) {
      fprintf(stream, "  %zd -> %zd [label=\"%c\"];\n", node->id, node->children[i]->id, (char)i);
      dump_trie_node(stream, node->children[i]);
    }
  }
}

void dump_trie(struct trie *trie) {
  FILE *stream = fopen("trie.dot", "w");

  fprintf(stream, "digraph trie {\n");
  fprintf(stream, "  node [shape=circle];\n");

  struct trie_node *node = trie->root;
  fprintf(stream, "  root [label=\"root\"];\n");
  for (size_t i = 0; i < 256; ++i) {
    if (node->children[i]) {
      fprintf(stream, "  root -> %zd [label=\"%c\"];\n", node->children[i]->id, (char)i);
      dump_trie_node(stream, node->children[i]);
    }
  }

  fprintf(stream, "}\n");
}

static void destroy_trie_node(struct trie_node *node) {
  if (!node) {
    return;
  }

  for (size_t i = 0; i < 256; ++i) {
    destroy_trie_node(node->children[i]);
  }
  free(node);
}

void destroy_trie(struct trie *trie) {
  destroy_trie_node(trie->root);
  free(trie);
}
