#include <stdlib.h>
#include <string.h>

#include "internal.h"
#include "lex.h"
#include "parse.h"

int parser_parse_enum_decl(struct parser *parser, struct ast_ty *into) {
  struct token token;

  into->ty = AST_TYPE_ENUM;
  into->oneof.enumty.no_wrapped_fields = 1;

  enum token_id peek = parser_peek(parser);
  if (peek == TOKEN_LT) {
    parser_consume_peeked(parser, NULL);

    struct ast_template_ty *last = into->oneof.enumty.templates;

    // parse templates
    peek = parser_peek(parser);
    while (peek != TOKEN_GT) {
      if (parser_consume(parser, &token, TOKEN_IDENTIFIER) < 0) {
        return -1;
      }

      struct ast_template_ty *template = calloc(1, sizeof(struct ast_template_ty));
      strncpy(template->name, token.value.identv.ident, 256);

      if (last) {
        last->next = template;
        last = template;
      } else {
        into->oneof.enumty.templates = template;
        last = template;
      }

      peek = parser_peek(parser);
      if (peek == TOKEN_COMMA) {
        parser_consume_peeked(parser, NULL);
        peek = parser_peek(parser);
      }
    }

    if (parser_consume(parser, NULL, TOKEN_GT) < 0) {
      return -1;
    }
  }

  if (parser_consume(parser, NULL, TOKEN_LBRACE) < 0) {
    return -1;
  }

  struct ast_enum_field *last = into->oneof.enumty.fields;
  peek = parser_peek(parser);
  uint64_t value = 0;
  while (peek != TOKEN_RBRACE) {
    if (parser_consume(parser, &token, TOKEN_IDENTIFIER) < 0) {
      return -1;
    }

    struct ast_enum_field *field = calloc(1, sizeof(struct ast_enum_field));
    strncpy(field->name, token.value.identv.ident, 256);
    field->value = value++;

    peek = parser_peek(parser);
    if (peek == TOKEN_LPAREN) {
      parser_consume_peeked(parser, NULL);

      // enum field with inner type
      field->has_inner = 1;
      field->parser_inner = parse_type(parser);

      if (parser_consume(parser, NULL, TOKEN_RPAREN) < 0) {
        return -1;
      }

      into->oneof.enumty.no_wrapped_fields = 0;
    }

    if (last) {
      last->next = field;
      last = field;
    } else {
      into->oneof.enumty.fields = field;
      last = field;
    }

    peek = parser_peek(parser);
    if (peek == TOKEN_COMMA) {
      parser_consume_peeked(parser, NULL);
      peek = parser_peek(parser);
    }
  }

  if (parser_consume(parser, NULL, TOKEN_RBRACE) < 0) {
    return -1;
  }

  return 0;
}
