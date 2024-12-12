#include "lex.h"

#include <ctype.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"
#include "tokens.h"

struct lex_state *new_lexer(FILE *stream, const char *filename) {
  struct lex_state *result = calloc(1, sizeof(struct lex_state));
  result->stream = stream;
  result->filename = malloc(strlen(filename) + 1);
  strcpy(result->filename, filename);
  result->loc.file = result->filename;
  initialize_keyword_trie(result);
  return result;
}

int lexer_eof(struct lex_state *state) {
  return feof(state->stream) && state->buf_head == state->buf_tail;
}

void lexer_locate(struct lex_state *state, struct lex_locator *loc) {
  memcpy(loc, &state->loc, sizeof(struct lex_locator));
}

void lexer_locate_str(struct lex_state *state, char *buf, size_t len) {
  snprintf(buf, len, "%s:%zu:%zu", state->loc.file, state->loc.line, state->loc.column + 1);
}

void lexer_update_loc(struct lex_state *state, struct lex_locator *loc) {
  memcpy(&state->loc, loc, sizeof(struct lex_locator));
}

void destroy_lexer(struct lex_state *state) {
  destroy_keyword_trie(state);
  free(state->filename);
  free(state);
}
