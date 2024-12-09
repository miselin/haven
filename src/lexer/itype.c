
#include <ctype.h>

#include "internal.h"
#include "lex.h"
#include "tokens.h"

int lex_integer_type(struct lex_state *state, struct token *token, char c) {
  int is_signed = c == 'i';

  size_t bits = 0;
  c = lex_getc(state);
  if (!isdigit(c)) {
    lex_unget(state, c);
    return 1;
  }

  while (isdigit(c)) {
    bits = bits * 10 + ((size_t)c - '0');
    c = lex_getc(state);
  }

  // put back the non-digit
  lex_unget(state, c);

  if (!bits) {
    lex_error(state, "integer type tokens must have at least 1 bit");
    return -1;
  }

  token->ident = is_signed ? TOKEN_TY_SIGNED : TOKEN_TY_UNSIGNED;
  token->value.tyv.dimension = bits;
  return 0;
}
