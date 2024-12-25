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

TEST(TrieTest, LookupRemoved) {
  struct trie *trie = new_trie();
  trie_insert(trie, "shared", (void *)1);
  trie_insert(trie, "share", (void *)2);
  trie_insert(trie, "sha", (void *)3);

  trie_remove(trie, "sha");

  EXPECT_EQ(trie_lookup(trie, "share"), (void *)2);
  EXPECT_EQ(trie_lookup(trie, "sha"), (void *)0);

  destroy_trie(trie);
}

TEST(TrieTest, InsertEmpty) {
  struct trie *trie = new_trie();
  trie_insert(trie, "", (void *)1);

  EXPECT_EQ(trie_lookup(trie, ""), (void *)0);

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

TEST(TrieTest, InsertLookupSharedSuffix) {
  struct trie *trie = new_trie();
  trie_insert(trie, "hat", (void *)1);
  trie_insert(trie, "mat", (void *)2);
  trie_insert(trie, "rat", (void *)3);
  trie_insert(trie, "cat", (void *)4);

  EXPECT_EQ(trie_lookup(trie, "hat"), (void *)1);
  EXPECT_EQ(trie_lookup(trie, "mat"), (void *)2);
  EXPECT_EQ(trie_lookup(trie, "rat"), (void *)3);
  EXPECT_EQ(trie_lookup(trie, "cat"), (void *)4);

  destroy_trie(trie);
}

TEST(TrieTest, InsertLookupSharedPrefix) {
  struct trie *trie = new_trie();
  trie_insert(trie, "tap", (void *)1);
  trie_insert(trie, "tar", (void *)2);
  trie_insert(trie, "tan", (void *)3);

  EXPECT_EQ(trie_lookup(trie, "tap"), (void *)1);
  EXPECT_EQ(trie_lookup(trie, "tar"), (void *)2);
  EXPECT_EQ(trie_lookup(trie, "tan"), (void *)3);

  EXPECT_EQ(trie_lookup(trie, "t"), (void *)0);

  destroy_trie(trie);
}

TEST(TrieTest, InsertLookupSharedInnerLetters) {
  struct trie *trie = new_trie();
  trie_insert(trie, "ata", (void *)1);
  trie_insert(trie, "bta", (void *)2);
  trie_insert(trie, "cta", (void *)3);

  EXPECT_EQ(trie_lookup(trie, "ata"), (void *)1);
  EXPECT_EQ(trie_lookup(trie, "bta"), (void *)2);
  EXPECT_EQ(trie_lookup(trie, "cta"), (void *)3);

  EXPECT_EQ(trie_lookup(trie, "t"), (void *)0);

  destroy_trie(trie);
}

TEST(TrieTest, InsertLookupSharedPrefixDifferentLengths) {
  struct trie *trie = new_trie();
  trie_insert(trie, "struct", (void *)1);
  trie_insert(trie, "store", (void *)2);
  trie_insert(trie, "str", (void *)3);

  dump_trie(trie);

  EXPECT_EQ(trie_lookup(trie, "struct"), (void *)1);
  EXPECT_EQ(trie_lookup(trie, "store"), (void *)2);
  EXPECT_EQ(trie_lookup(trie, "str"), (void *)3);

  destroy_trie(trie);
}
