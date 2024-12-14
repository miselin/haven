#ifndef _MATTC_SEMANTIC_H
#define _MATTC_SEMANTIC_H

#include "ast.h"

struct semantic;

// pass 0 expects to run before type checking
// pass 1 expects to run after type checking, as it depends on resolved types
struct semantic *semantic_new(struct ast_program *ast, struct compiler *compiler, int pass);
int semantic_run(struct semantic *semantic);
void semantic_destroy(struct semantic *purity);

#endif
