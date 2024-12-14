#ifndef _MATTC_PARSE_H
#define _MATTC_PARSE_H

#include "ast.h"
#include "compiler.h"
#include "lex.h"

struct parser;

struct parser *new_parser(struct lex_state *, struct compiler *);
int parser_run(struct parser *);
struct ast_program *parser_get_ast(struct parser *);
void destroy_parser(struct parser *);

#endif
