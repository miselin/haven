#include "internal.h"

char lex_getc(struct lex_state *state) {
  int c = 0;
  if (state->buf_head != state->buf_tail) {
    char head = state->buf[state->buf_head];
    state->buf_head = (state->buf_head + 1) % LEXER_BUFFER_SIZE;
    c = head;
  } else {
    c = fgetc(state->stream);
  }

  if (c == '\n') {
    state->prev_column = state->loc.column;
    state->loc.line++;
    state->loc.column = 0;
  } else {
    state->loc.column++;
  }

  if (c > 255) {
    return -1;
  }

  return (char)c;
}

void lex_unget(struct lex_state *state, char c) {
  if (c < 0) {
    return;
  }

  state->buf[state->buf_tail] = c;
  state->buf_tail = (state->buf_tail + 1) % LEXER_BUFFER_SIZE;

  if (state->loc.column > 0) {
    state->loc.column--;
  } else {
    state->loc.line--;
    state->loc.column = state->prev_column;
  }
}
