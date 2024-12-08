#include "lex.h"

#include <ctype.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tokens.h"

#define LEXER_BUFFER_SIZE 256

struct lex_state {
  FILE *stream;
  char buf[LEXER_BUFFER_SIZE];
  size_t buf_head;  // read from head
  size_t buf_tail;  // write to tail
};

static char lex_getc(struct lex_state *state);

static void lex_unget(struct lex_state *state, char c);

static int lex_check_either(struct lex_state *state, struct token *token, char expected,
                            int ident_true, int ident_false);

struct lex_state *new_lexer(FILE *stream) {
  struct lex_state *result = calloc(1, sizeof(struct lex_state));
  result->stream = stream;
  return result;
}

int lexer_eof(struct lex_state *state) {
  return feof(state->stream) && state->buf_head == state->buf_tail;
}

/*
static int lex_number(struct lex_state *state, uint64_t *out) {
  uint64_t v = 0;
  char c = lex_getc(state);
  while (isdigit(c)) {
    v = (v * 10) + (c - '0');
    c = lex_getc(state);
  }

  *out = v;
  lex_unget(state, c);
  return 0;
}
*/

static int lex_integer_type(struct lex_state *state, struct token *token, char c) {
  int is_signed = c == 'i';

  size_t bits = 0;
  c = lex_getc(state);
  if (!isdigit(c)) {
    lex_unget(state, c);
    return 1;
  }

  while (isdigit(c)) {
    bits = bits * 10 + (c - '0');
    c = lex_getc(state);
  }

  // put back the non-digit
  lex_unget(state, c);

  if (!bits) {
    fprintf(stderr, "integer type must have at least 1 bit\n");
    return -1;
  }

  token->ident = is_signed ? TOKEN_TY_SIGNED : TOKEN_TY_UNSIGNED;
  token->value.tyv.dimension = bits;
  return 0;
}

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

static int lex_integer(struct lex_state *state, struct token *token, char c) {
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

static int lex_vector_type(struct token *token) {
  // already got the fvec part
  long dim = strtol(&token->value.identv.ident[4], NULL, 10);
  if (dim < 0) {
    fprintf(stderr, "invalid vector dimension %ld\n", dim);
    return -1;
  }
  token->ident = TOKEN_TY_FVEC;
  token->value.tyv.dimension = dim;
  return 0;
}

static char escaped(char c) {
  switch (c) {
    case 'n':
      return '\n';
    case 't':
      return '\t';
    case 'r':
      return '\r';
    case '0':
      return '\0';
    case '"':
      return '"';
    default:
      return c;
  }
}

static int lex_string_literal(struct lex_state *state, struct token *token) {
  token->ident = TOKEN_STRING;

  // string literal
  size_t i = 0;
  int escape = 0;
  char c = lex_getc(state);
  while (c != '"' || escape) {
    if (c < 0) {
      // EOF in string literal
      return -1;
    }

    token->value.strv.s[i++] = escape ? escaped(c) : c;

    if (c == '\\' && !escape) {
      escape = 1;
      --i;  // remove the backslash
    } else {
      escape = 0;
    }

    c = lex_getc(state);
  }

  token->value.strv.s[i] = 0;
  token->value.strv.length = i;

  return 0;
}

static int lex_maybe_keyword(struct token *token) {
  if (!strcmp(token->value.identv.ident, "if")) {
    token->ident = TOKEN_KW_IF;
  } else if (!strcmp(token->value.identv.ident, "else")) {
    token->ident = TOKEN_KW_ELSE;
  } else if (!strcmp(token->value.identv.ident, "let")) {
    token->ident = TOKEN_KW_LET;
  } else if (!strcmp(token->value.identv.ident, "for")) {
    token->ident = TOKEN_KW_FOR;
  } else if (!strcmp(token->value.identv.ident, "while")) {
    token->ident = TOKEN_KW_WHILE;
  } else if (!strcmp(token->value.identv.ident, "break")) {
    token->ident = TOKEN_KW_BREAK;
  } else if (!strcmp(token->value.identv.ident, "continue")) {
    token->ident = TOKEN_KW_CONTINUE;
  } else if (!strcmp(token->value.identv.ident, "match")) {
    token->ident = TOKEN_KW_MATCH;
  } else if (!strcmp(token->value.identv.ident, "as")) {
    token->ident = TOKEN_KW_AS;
  } else if (!strcmp(token->value.identv.ident, "pub")) {
    token->ident = TOKEN_KW_PUB;
  } else if (!strcmp(token->value.identv.ident, "mut")) {
    token->ident = TOKEN_KW_MUT;
  } else if (!strcmp(token->value.identv.ident, "neg")) {
    token->ident = TOKEN_KW_NEG;
  } else if (!strcmp(token->value.identv.ident, "fn")) {
    token->ident = TOKEN_KW_FN;
  } else if (!strcmp(token->value.identv.ident, "iter")) {
    token->ident = TOKEN_KW_ITER;
  } else if (!strcmp(token->value.identv.ident, "ref")) {
    token->ident = TOKEN_KW_REF;
  } else if (!strcmp(token->value.identv.ident, "store")) {
    token->ident = TOKEN_KW_STORE;
  } else if (!strcmp(token->value.identv.ident, "load")) {
    token->ident = TOKEN_KW_LOAD;
  } else if (!strcmp(token->value.identv.ident, "ret")) {
    token->ident = TOKEN_KW_RETURN;
  } else if (!strcmp(token->value.identv.ident, "float")) {
    token->ident = TOKEN_TY_FLOAT;
  } else if (!strcmp(token->value.identv.ident, "str")) {
    token->ident = TOKEN_TY_STR;
  } else if (!strcmp(token->value.identv.ident, "char")) {
    token->ident = TOKEN_TY_CHAR;
  } else if (!strcmp(token->value.identv.ident, "void")) {
    token->ident = TOKEN_TY_VOID;
  } else if (!strncmp(token->value.identv.ident, "fvec", 4)) {
    return lex_vector_type(token);
  }

  return 0;
}

int lexer_token(struct lex_state *state, struct token *token) {
  char c = lex_getc(state);
  if (c < 0) {
    if (lexer_eof(state)) {
      token->ident = TOKEN_EOF;
      return 0;
    }
  }

  while (isspace(c)) {
    c = lex_getc(state);
  }

  if (isdigit(c)) {
    return lex_integer(state, token, c);
  }

  if (c == '"') {
    return lex_string_literal(state, token);
  }

  if (c == '\'') {
    c = lex_getc(state);
    if (c < 0) {
      fprintf(stderr, "EOF in character literal\n");
      return -1;
    }

    token->ident = TOKEN_CHAR;
    token->value.charv.c = c;

    c = lex_getc(state);
    if (c < 0) {
      fprintf(stderr, "EOF in character literal\n");
      return -1;
    }

    if (c != '\'') {
      fprintf(stderr, "too many tokens in character literal\n");
      return -1;
    }

    return 0;
  }

  if (c == 'i' || c == 'u') {
    int rc = lex_integer_type(state, token, c);
    if (rc <= 0) {
      return rc;
    }
  }

  switch (c) {
    case '+':
      return lex_check_either(state, token, '+', TOKEN_INCREMENT, TOKEN_PLUS);
    case '-':
      return lex_check_either(state, token, '-', TOKEN_DECREMENT, TOKEN_MINUS);
    case '*':
      token->ident = TOKEN_ASTERISK;
      break;
    case '/':
      token->ident = TOKEN_FSLASH;
      break;
    case '%':
      token->ident = TOKEN_PERCENT;
      break;
    case '=':
      return lex_check_either(state, token, '=', TOKEN_EQUALS, TOKEN_ASSIGN);
    case '(':
      token->ident = TOKEN_LPAREN;
      break;
    case ')':
      token->ident = TOKEN_RPAREN;
      break;
    case '{':
      token->ident = TOKEN_LBRACE;
      break;
    case '}':
      token->ident = TOKEN_RBRACE;
      break;
    case ';':
      token->ident = TOKEN_SEMI;
      break;
    case '&':
      return lex_check_either(state, token, '&', TOKEN_AND, TOKEN_BITAND);
    case '|':
      return lex_check_either(state, token, '|', TOKEN_OR, TOKEN_BITOR);
    case '!':
      return lex_check_either(state, token, '=', TOKEN_NE, TOKEN_NOT);
      break;
    case '<': {
      char next = lex_getc(state);
      if (next < 0) {
        token->ident = TOKEN_LT;
        return 0;
      }

      if (next == '<') {
        token->ident = TOKEN_LSHIFT;
      } else if (next == '=') {
        token->ident = TOKEN_LTE;
      } else {
        token->ident = TOKEN_LT;
        lex_unget(state, next);
      }

      return 0;
    } break;
    case '>': {
      char next = lex_getc(state);
      if (next < 0) {
        token->ident = TOKEN_GT;
        return 0;
      }

      if (next == '>') {
        token->ident = TOKEN_RSHIFT;
      } else if (next == '=') {
        token->ident = TOKEN_GTE;
      } else {
        token->ident = TOKEN_GT;
        lex_unget(state, next);
      }

      return 0;
    } break;
    case '^':
      token->ident = TOKEN_BITXOR;
      break;
    case ',':
      token->ident = TOKEN_COMMA;
      break;
    case ':':
      token->ident = TOKEN_COLON;
      break;
    case '.':
      return lex_check_either(state, token, '.', TOKEN_DOTDOT, TOKEN_PERIOD);
      break;
    case '"':
      token->ident = TOKEN_QUOTE;
      break;
    case '\'':
      token->ident = TOKEN_APOSTROPHE;
      break;
    case '[':
      token->ident = TOKEN_LBRACKET;
      break;
    case ']':
      token->ident = TOKEN_RBRACKET;
      break;
    case '~':
      token->ident = TOKEN_TILDE;
      break;
    default: {
      if (!isalpha(c)) {
        fprintf(stderr, "first character of identifier is not a letter\n");
        return -1;
      }

      size_t i = 0;
      token->ident = TOKEN_IDENTIFIER;
      token->value.identv.ident[i++] = c;

      c = lex_getc(state);
      if (c < 0) {
        // technically OK, EOF after the start of an identifier
        return 0;
      }

      while (isalpha(c) || isdigit(c) || c == '_') {
        if (c < 0) {
          // EOF during identifier is OK
          break;
        }
        token->value.identv.ident[i++] = c;

        c = lex_getc(state);
      }

      lex_unget(state, c);

      token->value.identv.ident[i] = 0;

      // turn identifiers that are keywords into tokens now that we parsed the
      // whole identifier
      fprintf(stderr, "lex: ident '%s'\n", token->value.identv.ident);
      return lex_maybe_keyword(token);
    }
  }

  fprintf(stderr, "lex: %c -> %d\n", c, token->ident);
  return 0;
}

void destroy_lexer(struct lex_state *state) {
  free(state);
}

static char lex_getc(struct lex_state *state) {
  if (state->buf_head != state->buf_tail) {
    char c = state->buf[state->buf_head];
    state->buf_head = (state->buf_head + 1) % LEXER_BUFFER_SIZE;
    return c;
  }

  return fgetc(state->stream);
}

static void lex_unget(struct lex_state *state, char c) {
  if (c < 0) {
    return;
  }
  state->buf[state->buf_tail] = c;
  state->buf_tail = (state->buf_tail + 1) % LEXER_BUFFER_SIZE;
}

static int lex_check_either(struct lex_state *state, struct token *token, char expected,
                            int ident_true, int ident_false) {
  char next = lex_getc(state);
  if (next < 0) {
    // not technically an error if we hit EOF here, as we are choosing between
    // two tokens and the first has already matched
    token->ident = ident_false;
    return 0;
  }

  if (next == expected) {
    token->ident = ident_true;
  } else {
    lex_unget(state, next);
    token->ident = ident_false;
  }

  return 0;
}

void print_token(struct token *token) {
  fprintf(stderr, "token %d", token->ident);
  switch (token->ident) {
    case TOKEN_INTEGER:
      fprintf(stderr, " '%lu' [sign=%d]", token->value.intv.val, token->value.intv.sign);
      break;
    case TOKEN_IDENTIFIER:
      fprintf(stderr, " '%s'", token->value.identv.ident);
      break;
  }
}
