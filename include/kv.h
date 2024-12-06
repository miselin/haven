#ifndef _MATTC_KV_H
#define _MATTC_KV_H

struct kv;

struct kv *new_kv();
void kv_insert(struct kv *kv, const char *key, void *value);
void *kv_lookup(struct kv *kv, const char *key);
void destroy_kv(struct kv *kv);

#endif