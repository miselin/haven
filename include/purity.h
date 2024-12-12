#ifndef _MATTC_PURITY_H
#define _MATTC_PURITY_H

#include "ast.h"

struct purity;

struct purity *purity_new(struct ast_program *ast);
int purity_run(struct purity *purity);
void purity_destroy(struct purity *purity);

#endif
