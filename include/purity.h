#ifndef _HAVEN_PURITY_H
#define _HAVEN_PURITY_H

#include "ast.h"

struct purity;

struct purity *purity_new(struct ast_program *ast, struct compiler *compiler);
int purity_run(struct purity *purity);
void purity_destroy(struct purity *purity);

#endif
