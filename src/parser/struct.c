#include <stdlib.h>
#include <string.h>

#include "internal.h"
#include "parse.h"

int parser_parse_struct_decl(struct parser *parser, struct ast_ty *into, int is_union) {
  if (parser_consume(parser, NULL, TOKEN_LBRACE) < 0) {
    return -1;
  }

  into->ty = AST_TYPE_STRUCT;
  into->oneof.structty.is_union = is_union;

  struct ast_struct_field *last = into->oneof.structty.fields;

  // parse fields
  while (parser_peek(parser) != TOKEN_RBRACE) {
    struct ast_ty field_ty = parse_type(parser);
    if (type_is_error(&field_ty)) {
      return -1;
    }

    struct token token;
    if (parser_consume(parser, &token, TOKEN_IDENTIFIER) < 0) {
      return -1;
    }

    struct ast_struct_field *field = calloc(1, sizeof(struct ast_struct_field));
    field->parsed_ty = field_ty;
    strncpy(field->name, token.value.identv.ident, 256);

    if (!last) {
      into->oneof.structty.fields = field;
    } else {
      last->next = field;
    }

    last = field;

    ++into->oneof.structty.num_fields;

    if (parser_consume(parser, NULL, TOKEN_SEMI) < 0) {
      return -1;
    }
  }

  if (!into->oneof.structty.fields) {
    parser_diag(1, parser, &parser->peek, "structs must have at least one field");
    return -1;
  }

  return parser_consume(parser, NULL, TOKEN_RBRACE);
}
