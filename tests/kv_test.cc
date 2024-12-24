#include "kv.h"

#include <gtest/gtest.h>

TEST(KVTest, InsertLookup) {
  struct kv *kv = new_kv();
  fprintf(stderr, "got kv %p\n", kv);

  kv_insert(kv, "hello", (void *)1);
  kv_insert(kv, "world", (void *)2);

  EXPECT_EQ(kv_lookup(kv, "hello"), (void *)1);
  EXPECT_EQ(kv_lookup(kv, "world"), (void *)2);

  destroy_kv(kv);
}

TEST(KVTest, LookupNotFound) {
  struct kv *kv = new_kv();
  kv_insert(kv, "hello", (void *)1);
  kv_insert(kv, "world", (void *)2);

  EXPECT_EQ(kv_lookup(kv, "foo"), nullptr);

  destroy_kv(kv);
}

TEST(KVTest, InsertOverwrite) {
  struct kv *kv = new_kv();
  kv_insert(kv, "hello", (void *)1);
  kv_insert(kv, "hello", (void *)2);

  // TODO: the compiler depends on this behavior, i.e. later inserts don't overwrite earlier ones
  // but that's totally a bug and should be fixed.
  EXPECT_EQ(kv_lookup(kv, "hello"), (void *)1);

  destroy_kv(kv);
}

TEST(KVTest, InsertEmpty) {
  struct kv *kv = new_kv();
  kv_insert(kv, "", (void *)1);

  EXPECT_EQ(kv_lookup(kv, ""), (void *)0);

  destroy_kv(kv);
}

TEST(KVTest, InsertLookupMany) {
  struct kv *kv = new_kv();
  kv_insert(kv, "hello", (void *)1);
  kv_insert(kv, "world", (void *)2);
  kv_insert(kv, "foo", (void *)3);
  kv_insert(kv, "bar", (void *)4);
  kv_insert(kv, "baz", (void *)5);

  EXPECT_EQ(kv_lookup(kv, "hello"), (void *)1);
  EXPECT_EQ(kv_lookup(kv, "world"), (void *)2);
  EXPECT_EQ(kv_lookup(kv, "foo"), (void *)3);
  EXPECT_EQ(kv_lookup(kv, "bar"), (void *)4);
  EXPECT_EQ(kv_lookup(kv, "baz"), (void *)5);

  destroy_kv(kv);
}

TEST(KVTest, InsertLookupSharedSuffix) {
  struct kv *kv = new_kv();
  kv_insert(kv, "hat", (void *)1);
  kv_insert(kv, "mat", (void *)2);
  kv_insert(kv, "rat", (void *)3);
  kv_insert(kv, "cat", (void *)4);

  EXPECT_EQ(kv_lookup(kv, "hat"), (void *)1);
  EXPECT_EQ(kv_lookup(kv, "mat"), (void *)2);
  EXPECT_EQ(kv_lookup(kv, "rat"), (void *)3);
  EXPECT_EQ(kv_lookup(kv, "cat"), (void *)4);

  destroy_kv(kv);
}

TEST(KVTest, InsertLookupSharedPrefix) {
  struct kv *kv = new_kv();
  kv_insert(kv, "tap", (void *)1);
  kv_insert(kv, "tar", (void *)2);
  kv_insert(kv, "tan", (void *)3);

  EXPECT_EQ(kv_lookup(kv, "tap"), (void *)1);
  EXPECT_EQ(kv_lookup(kv, "tar"), (void *)2);
  EXPECT_EQ(kv_lookup(kv, "tan"), (void *)3);

  EXPECT_EQ(kv_lookup(kv, "t"), (void *)0);

  destroy_kv(kv);
}

TEST(KVTest, InsertLookupSharedInnerLetters) {
  struct kv *kv = new_kv();
  kv_insert(kv, "ata", (void *)1);
  kv_insert(kv, "bta", (void *)2);
  kv_insert(kv, "cta", (void *)3);

  EXPECT_EQ(kv_lookup(kv, "ata"), (void *)1);
  EXPECT_EQ(kv_lookup(kv, "bta"), (void *)2);
  EXPECT_EQ(kv_lookup(kv, "cta"), (void *)3);

  EXPECT_EQ(kv_lookup(kv, "t"), (void *)0);

  destroy_kv(kv);
}

TEST(KVTest, InsertLookupSharedPrefixDifferentLengths) {
  struct kv *kv = new_kv();
  kv_insert(kv, "struct", (void *)1);
  kv_insert(kv, "store", (void *)2);
  kv_insert(kv, "str", (void *)3);

  EXPECT_EQ(kv_lookup(kv, "struct"), (void *)1);
  EXPECT_EQ(kv_lookup(kv, "store"), (void *)2);
  EXPECT_EQ(kv_lookup(kv, "str"), (void *)3);

  destroy_kv(kv);
}
