#ifndef _MATTC_CODEGEN_H
#define _MATTC_CODEGEN_H

#include "ast.h"

struct codegen;

struct codegen *new_codegen(struct ast_program *);
int codegen_run(struct codegen *);
char *codegen_ir(struct codegen *);
void codegen_dispose_ir(char *);
void destroy_codegen(struct codegen *);

#endif
