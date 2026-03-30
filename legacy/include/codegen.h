#ifndef _HAVEN_CODEGEN_H
#define _HAVEN_CODEGEN_H

#include "ast.h"

struct codegen;

struct codegen *new_codegen(struct ast_program *, struct compiler *compiler);
int codegen_run(struct codegen *);
int codegen_emit_ir(struct codegen *, FILE *);
int codegen_emit_bitcode(struct codegen *, FILE *);
int codegen_emit_obj(struct codegen *, FILE *);
int codegen_emit_asm(struct codegen *, FILE *);
char *codegen_ir(struct codegen *);
void codegen_dispose_ir(char *);
void destroy_codegen(struct codegen *);

#endif
