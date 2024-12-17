#ifndef _HAVEN_PARSE_H
#define _HAVEN_PARSE_H

#include "ast.h"
#include "compiler.h"
#include "lex.h"

struct parser;

struct parser_diag;

enum ParserDiagSeverity {
  Error,
  Warning,
};

struct parser *new_parser(struct lex_state *, struct compiler *);
int parser_run(struct parser *);
struct ast_program *parser_get_ast(struct parser *);
void destroy_parser(struct parser *);

struct parser_diag *parser_pop_diag(struct parser *);
void parser_free_diag(struct parser_diag *);

struct lex_locator *parser_diag_loc(struct parser_diag *);
const char *parser_diag_msg(struct parser_diag *);
enum ParserDiagSeverity parser_diag_severity(struct parser_diag *);

#endif
