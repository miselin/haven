
#include <ctype.h>

#include "internal.h"
#include "lex.h"
#include "tokens.h"

// TODO: consider returning the comment text - if it's in the AST it can be used for
// formatting/documentation tools

int lex_block_comment(struct lex_state *state, struct token *token) {
  // already saw the /*

  token->ident = TOKEN_COMMENTLONG;

  char c = lex_getc(state);
  while (c != '*') {
    if (c < 0) {
      // EOF in block comment
      return -1;
    }

    c = lex_getc(state);
  }

  c = lex_getc(state);
  if (c < 0) {
    // EOF in block comment
    return -1;
  }

  if (c == '/') {
    return 0;
  }

  lex_error(state, "unexpected termination of block comment\n");
  token->ident = TOKEN_UNKNOWN;
  return -1;
}

int lex_line_comment(struct lex_state *state, struct token *token) {
  // already saw the //

  char c = lex_getc(state);
  while (c != '\n') {
    if (c < 0) {
      // EOF in line comment
      return -1;
    }

    c = lex_getc(state);
  }

  token->ident = TOKEN_COMMENTLINE;
  return 0;
}
