
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"
#include "lex.h"
#include "tokens.h"

int lex_matrix_type(struct lex_state *state, struct token *token) {
  if (!isdigit(token->value.identv.ident[3])) {
    // it's an identifier that starts with mat, not a matrix type
    return 0;
  }

  size_t xoff = 3;
  size_t byoff = 0;
  for (size_t i = 3; i < 256; i++) {
    if (token->value.identv.ident[i] == 'x') {
      byoff = i;
      break;
    }
  }

  if (byoff == 0) {
    // not a matrix type.
    return 0;
  }

  char buf[64];
  strncpy(buf, token->value.identv.ident + xoff, byoff - xoff);

  // parse out the x dimension
  char *end = NULL;
  long dimx = strtol(&token->value.identv.ident[xoff], &end, 10);
  if (end && *end != 'x') {
    return 0;
  }

  // parse out the y dimension
  long dimy = strtol(token->value.identv.ident + byoff + 1, &end, 10);
  if (end && *end != 0) {
    return 0;
  }

  if (dimx <= 0) {
    lex_error(state, "invalid matrix column dimension %ld", dimx);
    return -1;
  }
  if (dimy <= 0) {
    lex_error(state, "invalid matrix row dimension %ld", dimy);
    return -1;
  }

  token->ident = TOKEN_TY_MAT;
  token->value.matv.x = (size_t)dimx;
  token->value.matv.y = (size_t)dimy;
  return 0;
}
