#ifndef _HAVEN_SCOPE_H
#define _HAVEN_SCOPE_H

struct scope;

struct scope *enter_scope(struct scope *parent);
struct scope *exit_scope(struct scope *scope);

void scope_insert(struct scope *scope, const char *name, void *value);
void *scope_lookup(struct scope *scope, const char *name, int recurse);

void *scope_iter(struct scope *scope);
void *scope_next(void **iter);
int scope_end(void *iter);

struct scope *scope_parent(struct scope *scope);

#endif
