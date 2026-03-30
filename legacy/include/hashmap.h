#ifndef _HAVEN_HASHMAP_H
#define _HAVEN_HASHMAP_H

#include <stdlib.h>

struct hashmap;

#ifdef __cplusplus
extern "C" {
#endif

struct hashmap *new_hash_map(size_t capacity);
int32_t hash_map_insert(struct hashmap *map, const char *key, void *value);
void *hash_map_lookup(struct hashmap *map, const char *key);
void destroy_hash_map(struct hashmap *map);

#ifdef __cplusplus
}
#endif

#endif
