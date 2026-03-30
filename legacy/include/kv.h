#ifndef _HAVEN_KV_H
#define _HAVEN_KV_H

struct kv;

#ifdef __cplusplus
extern "C" {
#endif

struct kv *new_kv(void);
void kv_insert(struct kv *kv, const char *key, void *value);
void *kv_lookup(struct kv *kv, const char *key);
void kv_delete(struct kv *kv, const char *key);
void destroy_kv(struct kv *kv);

void *kv_iter(struct kv *kv);
void *kv_next(void **iter);
int kv_end(void *iter);

#ifdef __cplusplus
}
#endif

#endif
