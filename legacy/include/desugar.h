#ifndef _HAVEN_DESUGAR_H
#define _HAVEN_DESUGAR_H

#include "ast.h"

struct desugar;

struct desugar *desugar_new(struct ast_program *ast, struct compiler *compiler);
int desugar_run(struct desugar *desugar);
void desugar_destroy(struct desugar *desugar);

#endif
