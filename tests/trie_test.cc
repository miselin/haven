#include "trie.h"

#include <gtest/gtest.h>

TEST(TrieTest, InsertLookup) {
  struct trie *trie = new_trie();
  trie_insert(trie, "hello", (void *)1);
  trie_insert(trie, "world", (void *)2);

  EXPECT_EQ(trie_lookup(trie, "hello"), (void *)1);
  EXPECT_EQ(trie_lookup(trie, "world"), (void *)2);

  destroy_trie(trie);
}

TEST(TrieTest, LookupNotFound) {
  struct trie *trie = new_trie();
  trie_insert(trie, "hello", (void *)1);
  trie_insert(trie, "world", (void *)2);

  EXPECT_EQ(trie_lookup(trie, "foo"), nullptr);

  destroy_trie(trie);
}

TEST(TrieTest, InsertOverwrite) {
  struct trie *trie = new_trie();
  trie_insert(trie, "hello", (void *)1);
  trie_insert(trie, "hello", (void *)2);

  EXPECT_EQ(trie_lookup(trie, "hello"), (void *)2);

  destroy_trie(trie);
}

TEST(TrieTest, InsertEmpty) {
  struct trie *trie = new_trie();
  trie_insert(trie, "", (void *)1);

  EXPECT_EQ(trie_lookup(trie, ""), (void *)1);

  destroy_trie(trie);
}

TEST(TrieTest, InsertLookupMany) {
  struct trie *trie = new_trie();
  trie_insert(trie, "hello", (void *)1);
  trie_insert(trie, "world", (void *)2);
  trie_insert(trie, "foo", (void *)3);
  trie_insert(trie, "bar", (void *)4);
  trie_insert(trie, "baz", (void *)5);

  EXPECT_EQ(trie_lookup(trie, "hello"), (void *)1);
  EXPECT_EQ(trie_lookup(trie, "world"), (void *)2);
  EXPECT_EQ(trie_lookup(trie, "foo"), (void *)3);
  EXPECT_EQ(trie_lookup(trie, "bar"), (void *)4);
  EXPECT_EQ(trie_lookup(trie, "baz"), (void *)5);

  destroy_trie(trie);
}
