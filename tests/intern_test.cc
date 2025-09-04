#include "intern.h"

#include <gtest/gtest.h>

TEST(InternTest, CreateDestroy) {
  struct intern *table = new_intern_table(16);
  ASSERT_NE(table, nullptr);
  destroy_intern_table(table);
}

TEST(InternTest, InternString) {
  struct intern *table = new_intern_table(16);
  ASSERT_NE(table, nullptr);

  const char *str = "test";
  struct interned *interned = intern(table, str);
  ASSERT_NE(interned, nullptr);
  ASSERT_STREQ(interned_string(interned), str);

  destroy_intern_table(table);
}

TEST(InternTest, InternedEquality) {
  struct intern *table = new_intern_table(16);
  ASSERT_NE(table, nullptr);

  const char *str = "test";
  struct interned *interned1 = intern(table, str);
  ASSERT_NE(interned1, nullptr);

  struct interned *interned2 = intern(table, str);
  ASSERT_NE(interned2, nullptr);

  ASSERT_EQ(interned1, interned2);

  destroy_intern_table(table);
}
