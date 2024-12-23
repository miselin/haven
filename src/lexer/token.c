#include <ctype.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"
#include "lex.h"
#include "tokens.h"

static int lex_check_either(struct lex_state *state, struct token *token, char expected,
                            enum token_id ident_true, enum token_id ident_false) {
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

int lexer_token(struct lex_state *state, struct token *token) {
  // load an initial locator in case of EOF or other error
  lexer_locate(state, &token->loc);

  char c = lex_getc(state);
  if (c < 0) {
    if (lexer_eof(state)) {
      token->ident = TOKEN_EOF;
      return 0;
    } else {
      return -1;
    }
  }

  while (isspace(c)) {
    if (c == '\n') {
      lexer_locate(state, &token->loc);
      token->ident = TOKEN_NEWLINE;
      return 0;
    }

    c = lex_getc(state);
  }

  // skipped spaces, this is the more correct locator
  lexer_locate(state, &token->loc);

  if (isdigit(c)) {
    return lex_numeric(state, token, c);
  }

  if (c == '"') {
    return lex_string_literal(state, token);
  }

  if (c == '\'') {
    c = lex_getc(state);
    if (c < 0) {
      lex_error(state, "EOF in character literal");
      return -1;
    }

    token->ident = TOKEN_CHAR;
    token->value.charv.c = c;

    c = lex_getc(state);
    if (c < 0) {
      lex_error(state, "EOF in character literal");
      return -1;
    }

    if (c != '\'') {
      lex_error(state, "too many tokens in character literal");
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

  if (c < 0) {
    if (lexer_eof(state)) {
      token->ident = TOKEN_EOF;
      return 0;
    } else {
      return -1;
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
    case '/': {
      char next = lex_getc(state);
      if (next == '*') {
        // block comment
        return lex_block_comment(state, token);
      } else if (next == '/') {
        // line comment
        return lex_line_comment(state, token);
      }

      token->ident = TOKEN_FSLASH;
    } break;
    case '%':
      token->ident = TOKEN_PERCENT;
      break;
    case '=': {
      char next = lex_getc(state);
      if (next < 0) {
        token->ident = TOKEN_ASSIGN;
        return 0;
      }

      if (next == '>') {
        token->ident = TOKEN_INTO;
      } else if (next == '=') {
        token->ident = TOKEN_EQUALS;
      } else {
        token->ident = TOKEN_ASSIGN;
        lex_unget(state, next);
      }

      return 0;
    } break;
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
      return lex_check_either(state, token, ':', TOKEN_COLONCOLON, TOKEN_COLON);
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
    case '#':
      token->ident = TOKEN_POUND;
      break;
    case '_':
      token->ident = TOKEN_UNDER;
      break;
    default: {
      if (!isalpha(c)) {
        token->ident = TOKEN_UNKNOWN;
        lex_error(state, "first character of identifier is not a letter");
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

      // turn identifiers that are keywords into tokens now that we parsed the whole identifier
      return lex_maybe_keyword_trie(state, token);
    }
  }

  return 0;
}
