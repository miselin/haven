#include <stdlib.h>
#include <string.h>

#include "internal.h"
#include "lex.h"
#include "parse.h"
#include "tokenstream.h"

void parser_diag(int fatal, struct parser *parser, struct token *token, const char *msg, ...) {
  if (parser->mute_diags) {
    return;
  }

  struct parser_diag *diag = calloc(1, sizeof(struct parser_diag));

  char msgbuf[1024];
  size_t offset = 0;
  int rc = 0;

  offset = (size_t)rc;

  va_list args;
  va_start(args, msg);
  rc = vsnprintf(msgbuf + offset, 1024 - offset, msg, args);
  va_end(args);

  if (rc < 0) {
    diag->message = strdup("failed to format diagnostic message");
    diag->severity = Error;
    if (token) {
      diag->loc = token->loc;
    }
    return;
  }

  diag->message = strdup(msgbuf);
  diag->severity = fatal ? Error : Warning;

  if (token) {
    diag->loc = token->loc;
  } else {
    lexer_locate(parser->lexer, &diag->loc);
  }

  if (parser->diags_tail) {
    parser->diags_tail->next = diag;
    parser->diags_tail = diag;
  } else {
    parser->diags = diag;
    parser->diags_tail = diag;
  }
}

void parser_mark(struct parser *parser) {
  if (parser_peek(parser) == TOKEN_UNKNOWN) {
    // no-op to avoid warnings
  }
  tokenstream_mark(parser->stream);
}

void parser_rewind(struct parser *parser) {
  tokenstream_rewind(parser->stream);
  parser->peek.ident = TOKEN_UNKNOWN;
}

void parser_commit(struct parser *parser) {
  tokenstream_commit(parser->stream);
}

enum token_id parser_peek_with_nl(struct parser *parser) {
  // Eat comments - but in the future we will want to carry them with the AST
  while (parser->peek.ident == TOKEN_UNKNOWN || parser->peek.ident == TOKEN_COMMENTLINE ||
         parser->peek.ident == TOKEN_COMMENTLONG) {
    parser->peek.ident = TOKEN_UNKNOWN;
    int rc = tokenstream_next_token(parser->stream, &parser->peek);
    if (rc < 0) {
      return TOKEN_UNKNOWN;
    } else if (parser->peek.ident == TOKEN_UNKNOWN) {
      return TOKEN_UNKNOWN;
    } else if (parser->peek.ident == TOKEN_EOF) {
      return TOKEN_EOF;
    }
  }

  return parser->peek.ident;
}

// peeks the next token in the stream, skipping over newlines
enum token_id parser_peek(struct parser *parser) {
  enum token_id peeked = TOKEN_UNKNOWN;
  do {
    peeked = parser_peek_with_nl(parser);
    if (peeked == TOKEN_NEWLINE) {
      // eat it.
      parser->peek.ident = TOKEN_UNKNOWN;
    }
  } while (peeked == TOKEN_NEWLINE && peeked != TOKEN_UNKNOWN && peeked != TOKEN_EOF);

  return peeked;
}

int parser_consume(struct parser *parser, struct token *token, enum token_id expected) {
  enum token_id rc = expected == TOKEN_NEWLINE ? parser_peek_with_nl(parser) : parser_peek(parser);
  if (rc == TOKEN_UNKNOWN || parser->peek.ident <= 0) {
    parser_diag(1, parser, NULL, "unexpected EOF or other error in token stream");
    return -1;
  } else if (parser->peek.ident != expected) {
    parser_diag(1, parser, &parser->peek, "unexpected token %s, wanted %s",
                token_id_to_string(parser->peek.ident), token_id_to_string(expected));
    return -1;
  }

  if (token) {
    memcpy(token, &parser->peek, sizeof(struct token));
  }

  parser->peek.ident = TOKEN_UNKNOWN;
  return 0;
}

int parser_consume_peeked(struct parser *parser, struct token *token) {
  return parser_consume(parser, token, parser->peek.ident);
}

struct ast_expr *wrap_cast(struct parser *parser, struct ast_expr *expr, struct ast_ty *ty) {
  if (!expr) {
    return NULL;
  }

  struct ast_expr *result = calloc(1, sizeof(struct ast_expr));
  result->type = AST_EXPR_TYPE_CAST;
  lexer_locate(parser->lexer, &result->loc);
  result->cast.expr = expr;
  result->cast.parsed_ty = *ty;
  return result;
}

int parse_braced_initializer(struct parser *parser, struct ast_expr *into) {
  struct ast_ty element_ty = parse_type(parser);
  if (parser_consume(parser, NULL, TOKEN_LBRACE) < 0) {
    return -1;
  }
  into->type = AST_EXPR_TYPE_CONSTANT;
  into->parsed_ty.ty = AST_TYPE_ARRAY;
  into->parsed_ty.array.element_ty = calloc(1, sizeof(struct ast_ty));
  *into->parsed_ty.array.element_ty = element_ty;
  into->list = parse_expression_list(parser, TOKEN_RBRACE, 0);
  if (!into->list) {
    parser_diag(1, parser, &parser->peek, "braced initializer must have at least one element");
    free(into->parsed_ty.array.element_ty);
    return -1;
  }
  into->parsed_ty.array.width = into->list->num_elements;
  if (parser_consume(parser, NULL, TOKEN_RBRACE) < 0) {
    return -1;
  }
  return 0;
}
