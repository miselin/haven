#include <stdlib.h>
#include <string.h>

#include "internal.h"
#include "lex.h"
#include "parse.h"

struct ast_expr *parser_parse_pattern_match(struct parser *parser) {
  struct ast_expr *result = calloc(1, sizeof(struct ast_expr));
  result->type = AST_EXPR_TYPE_PATTERN_MATCH;
  lexer_locate(parser->lexer, &result->loc);

  if (parser_consume(parser, &result->pattern_match.enum_name, TOKEN_IDENTIFIER) < 0) {
    goto fail;
  }
  if (parser_peek(parser) == TOKEN_COLONCOLON) {
    if (parser_consume(parser, NULL, TOKEN_COLONCOLON) < 0) {
      goto fail;
    }
    if (parser_consume(parser, &result->pattern_match.name, TOKEN_IDENTIFIER) < 0) {
      goto fail;
    }
  } else {
    // outer enum name will be derived from context, this is just the inner name
    result->pattern_match.name = result->pattern_match.enum_name;
    result->pattern_match.enum_name.ident = TOKEN_UNKNOWN;
  }

  if (parser_peek(parser) == TOKEN_LPAREN) {
    // enum pattern match
    parser_consume_peeked(parser, NULL);

    if (parser_peek(parser) == TOKEN_UNDER) {
      parser_consume_peeked(parser, NULL);
      result->pattern_match.bindings_ignored = 1;
    } else if (parser_peek(parser) == TOKEN_IDENTIFIER) {
      result->pattern_match.inner_vdecl = calloc(1, sizeof(struct ast_vdecl));
      result->pattern_match.inner_vdecl->ty = type_tbd();  // filled in by typecheck
      result->pattern_match.inner_vdecl->flags = DECL_FLAG_TEMPORARY | DECL_FLAG_MUT;
      parser_consume_peeked(parser, &result->pattern_match.inner_vdecl->ident);
    } else {
      goto fail;
    }

    if (parser_consume(parser, NULL, TOKEN_RPAREN) < 0) {
      goto fail;
    }
  }

  return result;

fail:
  free(result);
  return NULL;
}
