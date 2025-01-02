#ifndef _HAVEN_UTILITY_H
#define _HAVEN_UTILITY_H

#include <stdlib.h>

// ensures alignment to the given number of bytes
#define ALIGNED(x) alignas((x))

// byte offset of the given member in the struct
#define OFFSETOF(x, y) offsetof(x, y)

// the input parameter is not used
#define UNUSED(x) (void)x

#ifdef __APPLE__
#define WEAK __attribute__((weak_import))
#else
#define WEAK __attribute__((weak))
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct string_builder;

// Creates a new string builder with a dynamic capacity. It will resize itself as needed.
struct string_builder *new_string_builder(void);
// Creates a new string builder with a dynamic capacity, starting at the given capacity. It will
// resize itself as needed.
struct string_builder *new_string_builder_with_capacity(size_t);

// Creates a new string builder with the given buffer and length. Dynamic resizing is disabled.
struct string_builder *new_string_builder_for(char *, size_t);

// Append the given string to the string builder. Returns -1 if the builder is out of capacity.
int string_builder_append(struct string_builder *, const char *);

// Appends a formatted string to the string builder. Returns -1 if the builder is out of capacity.
int string_builder_appendf(struct string_builder *, const char *, ...)
    __attribute__((format(printf, 2, 0)));

// Retrieve the string from the string builder. For dynamic strings, the string is freed by
// free_string_builder. For static strings, the string is the same as passed to
// new_string_builder_for.
const char *string_builder_get(struct string_builder *);

size_t string_builder_len(struct string_builder *);

// Returns 1 if the string builder needed a resize but couldn't (e.g. due to static buffer).
int string_builder_needs_resize(struct string_builder *);

// Frees the string builder and its string if it was dynamically allocated.
void free_string_builder(struct string_builder *);

#ifdef __cplusplus
}
#endif

#endif
