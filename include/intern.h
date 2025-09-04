#ifndef _HAVEN_INTERN_H
#define _HAVEN_INTERN_H

#include <stdlib.h>

struct intern;
struct interned;

#ifdef __cplusplus
extern "C" {
#endif

struct intern *new_intern_table(size_t capacity);
void destroy_intern_table(struct intern *table);

struct interned *intern(struct intern *table, const char *string);
const char *interned_string(struct interned *interned);

#ifdef __cplusplus
}
#endif

#endif
