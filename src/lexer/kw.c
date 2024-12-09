#include <string.h>

#include "internal.h"
#include "tokens.h"

int lex_maybe_keyword(struct lex_state *state, struct token *token) {
  if (!strcmp(token->value.identv.ident, "if")) {
    token->ident = TOKEN_KW_IF;
  } else if (!strcmp(token->value.identv.ident, "else")) {
    token->ident = TOKEN_KW_ELSE;
  } else if (!strcmp(token->value.identv.ident, "let")) {
    token->ident = TOKEN_KW_LET;
  } else if (!strcmp(token->value.identv.ident, "for")) {
    token->ident = TOKEN_KW_FOR;
  } else if (!strcmp(token->value.identv.ident, "while")) {
    token->ident = TOKEN_KW_WHILE;
  } else if (!strcmp(token->value.identv.ident, "break")) {
    token->ident = TOKEN_KW_BREAK;
  } else if (!strcmp(token->value.identv.ident, "continue")) {
    token->ident = TOKEN_KW_CONTINUE;
  } else if (!strcmp(token->value.identv.ident, "match")) {
    token->ident = TOKEN_KW_MATCH;
  } else if (!strcmp(token->value.identv.ident, "as")) {
    token->ident = TOKEN_KW_AS;
  } else if (!strcmp(token->value.identv.ident, "pub")) {
    token->ident = TOKEN_KW_PUB;
  } else if (!strcmp(token->value.identv.ident, "mut")) {
    token->ident = TOKEN_KW_MUT;
  } else if (!strcmp(token->value.identv.ident, "neg")) {
    token->ident = TOKEN_KW_NEG;
  } else if (!strcmp(token->value.identv.ident, "fn")) {
    token->ident = TOKEN_KW_FN;
  } else if (!strcmp(token->value.identv.ident, "iter")) {
    token->ident = TOKEN_KW_ITER;
  } else if (!strcmp(token->value.identv.ident, "ref")) {
    token->ident = TOKEN_KW_REF;
  } else if (!strcmp(token->value.identv.ident, "store")) {
    token->ident = TOKEN_KW_STORE;
  } else if (!strcmp(token->value.identv.ident, "load")) {
    token->ident = TOKEN_KW_LOAD;
  } else if (!strcmp(token->value.identv.ident, "ret")) {
    token->ident = TOKEN_KW_RETURN;
  } else if (!strcmp(token->value.identv.ident, "float")) {
    token->ident = TOKEN_TY_FLOAT;
  } else if (!strcmp(token->value.identv.ident, "str")) {
    token->ident = TOKEN_TY_STR;
  } else if (!strcmp(token->value.identv.ident, "char")) {
    token->ident = TOKEN_TY_CHAR;
  } else if (!strcmp(token->value.identv.ident, "void")) {
    token->ident = TOKEN_TY_VOID;
  } else if (!strncmp(token->value.identv.ident, "fvec", 4)) {
    return lex_vector_type(state, token);
  }

  return 0;
}