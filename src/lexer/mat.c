
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"
#include "lex.h"
#include "tokens.h"

int lex_matrix_type(struct lex_state *state, struct token *token) {
  size_t xoff = 3;
  size_t byoff = 0;
  for (size_t i = 3; i < 256; i++) {
    if (token->value.identv.ident[i] == 'x') {
      byoff = i;
      break;
    }
  }

  if (byoff == 0) {
    lex_error(state, "invalid matrix type, missing 'x' separator");
    return -1;
  }

  char buf[64];
  strncpy(buf, token->value.identv.ident + xoff, byoff - xoff);

  // parse out the x dimension
  long dimx = strtol(&token->value.identv.ident[xoff], NULL, 10);

  // parse out the y dimension
  long dimy = strtol(token->value.identv.ident + byoff + 1, NULL, 10);

  if (dimx < 0) {
    lex_error(state, "invalid matrix column dimension %ld", dimx);
    return -1;
  }
  if (dimy < 0) {
    lex_error(state, "invalid matrix row dimension %ld", dimy);
    return -1;
  }

  token->ident = TOKEN_TY_MAT;
  token->value.matv.x = (size_t)dimx;
  token->value.matv.y = (size_t)dimy;
  return 0;
}
