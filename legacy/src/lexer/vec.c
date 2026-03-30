
#include <ctype.h>
#include <stdlib.h>

#include "internal.h"
#include "lex.h"
#include "tokens.h"
#include "utility.h"

int lex_vector_type(struct lex_state *state, struct token *token) {
  UNUSED(state);

  if (!isdigit(token->value.identv.ident[4])) {
    // it's an identifier that starts with fvec, not a vector type
    return 0;
  }

  // already got the fvec part
  char *end = NULL;
  long dim = strtol(&token->value.identv.ident[4], &end, 10);
  if (dim < 0) {
    // parse as identifier instead of fvec
    return 0;
  }

  if (end && *end != 0) {
    // parse as identifier instead of fvec
    return 0;
  }

  token->ident = TOKEN_TY_FVEC;
  token->value.tyv.dimension = (size_t)dim;
  return 0;
}
