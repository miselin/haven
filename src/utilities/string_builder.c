#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "utility.h"

struct string_builder {
  char *buf;
  size_t len;
  size_t cap;
  int is_static;
  int wanted_resize;
};

static void resize_string_builder(struct string_builder *builder, size_t new_cap) {
  builder->buf = realloc(builder->buf, new_cap);
  builder->cap = new_cap;
}

struct string_builder *new_string_builder(void) {
  return new_string_builder_with_capacity(256);
}

struct string_builder *new_string_builder_with_capacity(size_t len) {
  struct string_builder *builder = calloc(1, sizeof(struct string_builder));
  builder->cap = len;
  builder->buf = malloc(builder->cap);
  builder->buf[0] = 0;
  return builder;
}

struct string_builder *new_string_builder_for(char *buf, size_t len) {
  struct string_builder *builder = calloc(1, sizeof(struct string_builder));
  builder->buf = buf;
  builder->len = 0;
  builder->cap = len;
  builder->is_static = 1;
  return builder;
}

int string_builder_append(struct string_builder *builder, const char *str) {
  return string_builder_appendf(builder, "%s", str);
}

int string_builder_appendf(struct string_builder *builder, const char *fmt, ...) {
  va_list ap1, ap2;
  va_start(ap1, fmt);
  va_copy(ap2, ap1);

  size_t required = 0;
  size_t remaining = builder->cap - builder->len;

  int rc = vsnprintf(builder->buf + builder->len, remaining, fmt, ap1);
  va_end(ap1);

  if (rc >= 0) {
    required = (size_t)rc;
    rc = 0;
  }

  if (required < remaining) {
    builder->len += required;

    va_end(ap2);
    return 0;
  }

  if (builder->is_static) {
    va_end(ap2);

    // can't resize, return the total buffer size needed
    builder->wanted_resize = 1;
    return (int)(builder->len + required);
  }

  resize_string_builder(builder, builder->cap + ((size_t)required + 1));

  required = 0;

  remaining = builder->cap - builder->len;
  rc = vsnprintf(builder->buf + builder->len, remaining, fmt, ap2);
  if (rc >= 0) {
    required = (size_t)rc;

    if (required > remaining) {
      // still failed to fit into the buffer somehow
      required = 0;
      rc = -1;
    } else {
      rc = 0;
    }
  }

  builder->len += required;

  va_end(ap2);

  return rc;
}

const char *string_builder_get(struct string_builder *builder) {
  return builder->buf;
}

size_t string_builder_len(struct string_builder *builder) {
  return builder->len;
}

int string_builder_needs_resize(struct string_builder *builder) {
  return builder->wanted_resize;
}

void free_string_builder(struct string_builder *builder) {
  if (!builder->is_static) {
    free(builder->buf);
  }
  free(builder);
}
