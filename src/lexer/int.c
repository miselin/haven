
#include <ctype.h>
#include <string.h>

#include "internal.h"
#include "lex.h"
#include "tokens.h"

static int isdigit_base(char c, size_t base) {
  if (base == 10) {
    return isdigit(c);
  }

  if (base == 16) {
    return isxdigit(c);
  }

  if (base == 8) {
    return c >= '0' && c <= '7';
  }

  if (base == 2) {
    return c == '0' || c == '1';
  }

  return 0;
}

int lex_numeric(struct lex_state *state, struct token *token, char c) {
  size_t base = 10;
  if (c == '0') {
    c = lex_getc(state);
    switch (c) {
      case 'x':
        base = 16;
        break;
      case 'b':
        base = 2;
        break;
      case 'o':
        base = 8;
        break;
    }

    if (base != 10) {
      c = lex_getc(state);
    } else {
      lex_unget(state, c);
      c = '0';
    }
  }

  char fpbuf[256] = {0};
  char *buf = &fpbuf[0];

  uint64_t vs[2];
  int is_float = 0;

  for (int pass = 0; pass < 2; ++pass) {
    // probably an integer
    uint64_t v = 0;
    while (isdigit_base(c, base)) {
      v = (v * base) + (c - '0');
      *buf++ = c;
      c = lex_getc(state);
    }

    vs[pass] = v;

    if (c == '.') {
      is_float = 1;
      buf = &fpbuf[128];
      c = lex_getc(state);
    } else {
      break;
    }
  }

  if (is_float) {
    token->ident = TOKEN_FLOAT;
    memset(token->value.floatv.buf, 0, 256);
    token->value.floatv.length =
        snprintf(token->value.floatv.buf, 256, "%s.%s", &fpbuf[0], &fpbuf[128]);
  } else {
    token->ident = TOKEN_INTEGER;
    token->value.intv.val = vs[0];
  }

  lex_unget(state, c);
  return 0;
}