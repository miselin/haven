#ifndef _HAVEN_LEX_H
#define _HAVEN_LEX_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "compiler.h"
#include "tokens.h"

struct lex_state;

struct lex_locator {
  size_t line;
  size_t column;
  char file[256];
};

struct token_int_value {
  uint64_t val;
  char sign;
};

struct token_char_value {
  char c;
};

struct token_str_value {
  char s[256];
  size_t length;
};

struct token_ident_value {
  char ident[256];
};

struct token_type_value {
  size_t dimension;
};

struct token_float_value {
  char buf[256];
  size_t length;
};

struct token_comment_value {
  char buf[256];
  size_t length;
};

struct token_matrix_value {
  size_t x;
  size_t y;
};

union token_value {
  struct token_int_value intv;
  struct token_char_value charv;
  struct token_str_value strv;
  struct token_ident_value identv;
  struct token_type_value tyv;
  struct token_float_value floatv;
  struct token_comment_value commentv;
  struct token_matrix_value matv;
};

struct token {
  enum token_id ident;
  int is_keyword;

  union token_value value;

  struct lex_locator loc;
};

#ifdef __cplusplus
extern "C" {
#endif

struct lex_state *new_lexer(FILE *, const char *filename, struct compiler *compiler);
int lexer_eof(struct lex_state *);
int lexer_token(struct lex_state *, struct token *);
void lexer_locate(struct lex_state *, struct lex_locator *);
void lexer_locate_str(struct lex_state *, char *buf, size_t len);
void destroy_lexer(struct lex_state *);

void lexer_update_loc(struct lex_state *, struct lex_locator *);

void print_token(struct token *);

#ifdef __cplusplus
};
#endif

#endif
