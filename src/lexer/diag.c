
#include <stdarg.h>
#include <stdio.h>

#include "internal.h"
#include "lex.h"
#include "tokens.h"

void lex_error(struct lex_state *state, const char *fmt, ...) {
  char locbuf[64] = {0};
  lexer_locate_str(state, locbuf, 64);
  fputs(locbuf, stderr);
  fputs(": ", stderr);

  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);

  fprintf(stderr, "\n");
}

void print_token(struct token *token) {
  fprintf(stderr, "token %d", token->ident);
  switch (token->ident) {
    case TOKEN_INTEGER:
      fprintf(stderr, " '%lu' [sign=%d]", token->value.intv.val, token->value.intv.sign);
      break;
    case TOKEN_IDENTIFIER:
      fprintf(stderr, " '%s'", token->value.identv.ident);
      break;
  }
}
