#include <stdlib.h>
#include <string.h>

#include "internal.h"
#include "parse.h"

struct ast_ty parse_type(struct parser *parser) {
  struct token token;

  struct ast_ty result;
  memset(&result, 0, sizeof(struct ast_ty));
  result.ty = AST_TYPE_ERROR;

  enum token_id peek = parser_peek(parser);
  if (peek == TOKEN_TY_SIGNED || peek == TOKEN_TY_UNSIGNED) {
    parser_consume_peeked(parser, &token);

    result.ty = AST_TYPE_INTEGER;
    result.integer.is_signed = peek == TOKEN_TY_SIGNED;
    result.integer.width = token.value.tyv.dimension;
  } else if (peek == TOKEN_TY_STR) {
    parser_consume_peeked(parser, &token);

    result.ty = AST_TYPE_STRING;
  } else if (peek == TOKEN_TY_CHAR) {
    parser_consume_peeked(parser, &token);

    result.ty = AST_TYPE_INTEGER;
    result.integer.is_signed = 1;
    result.integer.width = 8;
  } else if (peek == TOKEN_TY_FLOAT) {
    parser_consume_peeked(parser, &token);

    result.ty = AST_TYPE_FLOAT;
  } else if (peek == TOKEN_TY_FVEC) {
    parser_consume_peeked(parser, &token);

    result.ty = AST_TYPE_FVEC;
    result.fvec.width = token.value.tyv.dimension;
  } else if (peek == TOKEN_TY_VOID) {
    parser_consume_peeked(parser, &token);

    result.ty = AST_TYPE_VOID;
  } else if (peek == TOKEN_IDENTIFIER) {
    parser_consume_peeked(parser, &token);

    result.ty = AST_TYPE_CUSTOM;
    strncpy(result.name, token.value.identv.ident, 256);

    peek = parser_peek(parser);
    if (peek == TOKEN_LT) {
      struct ast_ty *tmplty = calloc(1, sizeof(struct ast_ty));
      tmplty->ty = AST_TYPE_TEMPLATE;
      tmplty->tmpl.outer = calloc(1, sizeof(struct ast_ty));
      memcpy(tmplty->tmpl.outer, &result, sizeof(struct ast_ty));

      struct ast_template_ty *inner_prev = NULL;

      parser_consume_peeked(parser, NULL);
      while (parser_peek(parser) != TOKEN_GT) {
        struct ast_template_ty *inner_ty = calloc(1, sizeof(struct ast_template_ty));
        inner_ty->parsed_ty = parse_type(parser);
        inner_ty->is_resolved = 1;

        if (inner_prev == NULL) {
          tmplty->tmpl.inners = inner_ty;
        } else {
          inner_prev->next = inner_ty;
        }

        inner_prev = inner_ty;

        if (parser_peek(parser) == TOKEN_GT) {
          break;
        }

        if (parser_consume(parser, NULL, TOKEN_COMMA) < 0) {
          result.ty = AST_TYPE_ERROR;
          return result;
        }
      }
      if (parser_consume(parser, NULL, TOKEN_GT) < 0) {
        result.ty = AST_TYPE_ERROR;
        return result;
      }

      memcpy(&result, tmplty, sizeof(struct ast_ty));
      free(tmplty);
    }
  } else if (peek == TOKEN_KW_STRUCT || peek == TOKEN_KW_UNION) {
    parser_consume_peeked(parser, NULL);

    if (parser_parse_struct_decl(parser, &result, peek == TOKEN_KW_UNION) < 0) {
      result.ty = AST_TYPE_ERROR;
      return result;
    }
  } else if (peek == TOKEN_KW_ENUM) {
    parser_consume_peeked(parser, NULL);

    if (parser_parse_enum_decl(parser, &result) < 0) {
      result.ty = AST_TYPE_ERROR;
      return result;
    }
  } else if (peek == TOKEN_TY_MAT) {
    parser_consume_peeked(parser, &token);

    result.ty = AST_TYPE_MATRIX;
    result.matrix.cols = token.value.matv.x;
    result.matrix.rows = token.value.matv.y;
  } else {
    parser_diag(1, parser, &parser->peek, "unexpected token of type %s when parsing type\n",
                token_id_to_string(peek));
  }

  // <ty>* == raw pointer
  while (parser_peek(parser) == TOKEN_ASTERISK) {
    parser_consume_peeked(parser, NULL);
    result = ptr_type(result);
  }

  // <ty>^ == boxed
  while (parser_peek(parser) == TOKEN_BITXOR) {
    parser_consume_peeked(parser, NULL);
    result = box_type(result);
  }

  if (parser_peek(parser) == TOKEN_LBRACKET) {
    parser_consume_peeked(parser, NULL);
    struct ast_ty *element_ty = calloc(1, sizeof(struct ast_ty));
    memcpy(element_ty, &result, sizeof(struct ast_ty));
    result.array.element_ty = element_ty;
    if (parser_consume(parser, &token, TOKEN_INTEGER) < 0) {
      result.ty = AST_TYPE_ERROR;
      free(element_ty);
      return result;
    }
    result.array.width = token.value.tyv.dimension;
    if (parser_consume(parser, NULL, TOKEN_RBRACKET) < 0) {
      result.ty = AST_TYPE_ERROR;
      free(element_ty);
      return result;
    }

    result.ty = AST_TYPE_ARRAY;
  }

  return result;
}
