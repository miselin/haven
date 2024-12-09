#ifndef _MATTC_LEX_H
#define _MATTC_LEX_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "tokens.h"

struct lex_state;

struct lex_locator {
  size_t line;
  size_t column;
};

struct token {
  enum token_id ident;
  union {
    struct {
      uint64_t val;
      char sign;
    } intv;
    struct {
      char c;
    } charv;
    struct {
      char s[256];
      size_t length;
    } strv;
    struct {
      char ident[256];
    } identv;
    struct {
      size_t dimension;
    } tyv;
    struct {
      char buf[256];
      size_t length;
    } floatv;
  } value;

  struct lex_locator loc;
};

struct lex_state *new_lexer(FILE *);
int lexer_eof(struct lex_state *);
int lexer_token(struct lex_state *, struct token *);
void lexer_locate(struct lex_state *, struct lex_locator *);
void lexer_locate_str(struct lex_state *, char *buf, size_t len);
void destroy_lexer(struct lex_state *);

void print_token(struct token *);

#endif