#include <stdlib.h>
#include <string.h>

#include "internal.h"
#include "lex.h"
#include "parse.h"
#include "tokens.h"

int parse_block(struct parser *parser, struct ast_block *into) {
  struct ast_stmt *last = NULL;
  lexer_locate(parser->lexer, &into->loc);

  /**
   * { <stmt>* }
   */

  struct token token;
  if (parser_consume(parser, &token, TOKEN_LBRACE) < 0) {
    return -1;
  }
  int ended_semi = 0;
  while (1) {
    enum token_id peek = parser_peek(parser);
    if (peek == TOKEN_RBRACE) {
      break;
    } else if (peek == TOKEN_UNKNOWN) {
      parser_diag(1, parser, NULL, "unexpected lexer token in block");
    } else {
      ended_semi = 0;
    }

    struct ast_stmt *stmt = parse_statement(parser, &ended_semi);
    if (!stmt) {
      return -1;
    }

    if (last) {
      last->next = stmt;
    } else {
      into->stmt = stmt;
    }

    last = stmt;

    if (!ended_semi) {
      peek = parser_peek(parser);
      if (peek == TOKEN_COMMA) {
        // - If this isn't the first statement, it's a parser error
        // - Otherwise, we need to swap to creating an initializer that will be typed by typecheck
        // conversion
        parser_diag(1, parser, NULL, "bare initializer syntax is a TODO right now");
        return -1;
      }
    }
  }
  if (parser_consume(parser, &token, TOKEN_RBRACE) < 0) {
    return -1;
  }

  if (ended_semi || !last || last->type != AST_STMT_TYPE_EXPR) {
    // add a void yielding expression
    struct ast_stmt *stmt = calloc(1, sizeof(struct ast_stmt));
    stmt->type = AST_STMT_TYPE_EXPR;
    lexer_locate(parser->lexer, &stmt->loc);
    stmt->stmt.expr = calloc(1, sizeof(struct ast_expr));
    stmt->stmt.expr->type = AST_EXPR_TYPE_VOID;
    if (last) {
      last->next = stmt;
    } else {
      into->stmt = stmt;
    }
  }

  return 0;
}
