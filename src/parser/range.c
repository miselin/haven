#include <stdlib.h>
#include <string.h>

#include "internal.h"
#include "parse.h"

struct ast_range parse_range(struct parser *parser) {
  struct ast_range result;

  // ranges are all i64s (for now...)
  struct ast_ty ty;
  memset(&ty, 0, sizeof(struct ast_ty));
  ty.ty = AST_TYPE_INTEGER;
  ty.oneof.integer.is_signed = 1;
  ty.oneof.integer.width = 64;

  result.start = wrap_cast(parser, parse_expression(parser), &ty);
  if (parser_consume(parser, NULL, TOKEN_COLON) < 0) {
    // TODO: parse_range needs to be able to return an error
    result.start = NULL;
    return result;
  }
  result.end = wrap_cast(parser, parse_expression(parser), &ty);

  if (parser_peek(parser) == TOKEN_COLON) {
    parser_consume_peeked(parser, NULL);
    result.step = wrap_cast(parser, parse_expression(parser), &ty);
  } else {
    result.step = NULL;
  }

  return result;
}
