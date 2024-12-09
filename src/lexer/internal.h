#ifndef _MATTC_LEXER_INTERNAL_H
#define _MATTC_LEXER_INTERNAL_H

#include <stddef.h>
#include <stdio.h>

#include "lex.h"

#define LEXER_BUFFER_SIZE 256

struct lex_state {
  FILE *stream;
  char buf[LEXER_BUFFER_SIZE];
  size_t buf_head;     // read from head
  size_t buf_tail;     // write to tail
  size_t prev_column;  // column before a newline (for unget)

  struct lex_locator loc;
};

char lex_getc(struct lex_state *state);

void lex_unget(struct lex_state *state, char c);

int lex_maybe_keyword(struct lex_state *state, struct token *token);

void lex_error(struct lex_state *state, const char *fmt, ...);

// lex an integer or floating point literal
int lex_numeric(struct lex_state *state, struct token *token, char c);

// lex an integer type (e.g. i32)
int lex_integer_type(struct lex_state *state, struct token *token, char c);

// lex the width of a vector type (e.g. fvec4)
int lex_vector_type(struct lex_state *state, struct token *token);

// lex a string literal
int lex_string_literal(struct lex_state *state, struct token *token);

#endif