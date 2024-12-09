
#include <ctype.h>

#include "internal.h"
#include "lex.h"
#include "tokens.h"

static char escaped(char c) {
  switch (c) {
    case 'n':
      return '\n';
    case 't':
      return '\t';
    case 'r':
      return '\r';
    case '0':
      return '\0';
    case '"':
      return '"';
    default:
      return c;
  }
}

int lex_string_literal(struct lex_state *state, struct token *token) {
  token->ident = TOKEN_STRING;

  // string literal
  size_t i = 0;
  int escape = 0;
  char c = lex_getc(state);
  while (c != '"' || escape) {
    if (c < 0) {
      // EOF in string literal
      return -1;
    }

    token->value.strv.s[i++] = escape ? escaped(c) : c;

    if (c == '\\' && !escape) {
      escape = 1;
      --i;  // remove the backslash
    } else {
      escape = 0;
    }

    c = lex_getc(state);
  }

  token->value.strv.s[i] = 0;
  token->value.strv.length = i;

  return 0;
}