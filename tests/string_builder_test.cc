#include <gtest/gtest.h>

#include "utility.h"

TEST(StringBuilderTest, SimpleAppend) {
  struct string_builder *builder = new_string_builder();

  string_builder_append(builder, "Hello, ");
  string_builder_append(builder, "World!");

  EXPECT_STREQ(string_builder_get(builder), "Hello, World!");

  free_string_builder(builder);
}

TEST(StringBuilderTest, FormatAppend) {
  struct string_builder *builder = new_string_builder();

  string_builder_appendf(builder, "%s, %s!", "Hello", "World");

  EXPECT_STREQ(string_builder_get(builder), "Hello, World!");

  free_string_builder(builder);
}

TEST(StringBuilderTest, MultipleFormatAppend) {
  struct string_builder *builder = new_string_builder();

  string_builder_appendf(builder, "%s, ", "one");
  string_builder_appendf(builder, "%s, ", "two");
  string_builder_appendf(builder, "%s, ", "three");
  string_builder_append(builder, "four");

  EXPECT_STREQ(string_builder_get(builder), "one, two, three, four");

  free_string_builder(builder);
}

TEST(StringBuilderTest, NoResizeForStatic) {
  char buf[8];
  struct string_builder *builder = new_string_builder_for(buf, 8);

  EXPECT_EQ(string_builder_appendf(builder, "%s,", "1111"), 0);
  EXPECT_EQ(string_builder_appendf(builder, "%s", "1111"), 9);

  EXPECT_STREQ(string_builder_get(builder), "1111,11");

  free_string_builder(builder);
}

TEST(StringBuilderTest, AutoResize) {
  struct string_builder *builder = new_string_builder_with_capacity(4);

  EXPECT_EQ(string_builder_appendf(builder, "%s,", "1111"), 0);
  EXPECT_EQ(string_builder_appendf(builder, "%s", "1111"), 0);

  EXPECT_STREQ(string_builder_get(builder), "1111,1111");

  free_string_builder(builder);
}
