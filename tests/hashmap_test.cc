#include "hashmap.h"

#include <gtest/gtest.h>

TEST(HashMapTest, CreateDestroy) {
  struct hashmap *map = new_hash_map(16);
  ASSERT_NE(map, nullptr);
  destroy_hash_map(map);
}

TEST(HashMapTest, LookupEmpty) {
  struct hashmap *map = new_hash_map(16);
  ASSERT_NE(map, nullptr);
  ASSERT_EQ(hash_map_lookup(map, "key1"), nullptr);
  destroy_hash_map(map);
}

TEST(HashMapTest, InsertLookup) {
  struct hashmap *map = new_hash_map(16);
  ASSERT_NE(map, nullptr);

  int value1 = 42;
  int value2 = 84;
  ASSERT_EQ(hash_map_insert(map, "key1", &value1), 0);
  ASSERT_EQ(hash_map_insert(map, "key2", &value2), 0);
  ASSERT_EQ(hash_map_lookup(map, "key1"), &value1);
  ASSERT_EQ(hash_map_lookup(map, "key2"), &value2);

  destroy_hash_map(map);
}

TEST(HashMapTest, LookupNonExistent) {
  struct hashmap *map = new_hash_map(16);
  ASSERT_NE(map, nullptr);
  int value1 = 42;
  int value2 = 84;
  ASSERT_EQ(hash_map_insert(map, "key1", &value1), 0);
  ASSERT_EQ(hash_map_insert(map, "key2", &value2), 0);
  ASSERT_EQ(hash_map_lookup(map, "key3"), nullptr);
  destroy_hash_map(map);
}

TEST(HashMapTest, InsertDuplicate) {
  struct hashmap *map = new_hash_map(16);
  ASSERT_NE(map, nullptr);

  int value1 = 42;
  int value2 = 84;
  ASSERT_EQ(hash_map_insert(map, "key1", &value1), 0);
  ASSERT_EQ(hash_map_insert(map, "key1", &value2), 0);
  ASSERT_EQ(hash_map_lookup(map, "key1"), &value2);

  destroy_hash_map(map);
}

TEST(HashMapTest, LookupNonExistentFull) {
  struct hashmap *map = new_hash_map(4);
  ASSERT_NE(map, nullptr);

  int value = 42;
  ASSERT_EQ(hash_map_insert(map, "key1", &value), 0);
  ASSERT_EQ(hash_map_insert(map, "key2", &value), 0);
  ASSERT_EQ(hash_map_insert(map, "key3", &value), 0);
  ASSERT_EQ(hash_map_insert(map, "key4", &value), 0);
  ASSERT_EQ(hash_map_lookup(map, "key5"), nullptr);

  destroy_hash_map(map);
}

TEST(HashMapTest, InsertTooMany) {
  struct hashmap *map = new_hash_map(4);
  ASSERT_NE(map, nullptr);

  int value = 42;
  int value2 = 84;
  ASSERT_EQ(hash_map_insert(map, "key1", &value), 0);
  ASSERT_EQ(hash_map_insert(map, "key2", &value), 0);
  ASSERT_EQ(hash_map_insert(map, "key3", &value), 0);
  ASSERT_EQ(hash_map_insert(map, "key4", &value), 0);
  ASSERT_EQ(hash_map_insert(map, "key5", &value), 1);

  // ensure overwrites still work at full capacity
  ASSERT_EQ(hash_map_lookup(map, "key1"), &value);
  ASSERT_EQ(hash_map_insert(map, "key1", &value2), 0);
  ASSERT_EQ(hash_map_lookup(map, "key1"), &value2);

  destroy_hash_map(map);
}
