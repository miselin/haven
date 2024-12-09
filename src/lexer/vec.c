
#include <ctype.h>
#include <stdlib.h>

#include "internal.h"
#include "lex.h"
#include "tokens.h"

int lex_vector_type(struct lex_state *state, struct token *token) {
  // already got the fvec part
  long dim = strtol(&token->value.identv.ident[4], NULL, 10);
  if (dim < 0) {
    lex_error(state, "invalid vector dimension %ld", dim);
    return -1;
  }
  token->ident = TOKEN_TY_FVEC;
  token->value.tyv.dimension = (size_t)dim;
  return 0;
}
