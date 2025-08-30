#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "internal.h"
#include "lex.h"
#include "parse.h"
#include "tokens.h"
#include "types.h"

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
  int is_initializer = 0;
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

    if (!ended_semi) {
      peek = parser_peek(parser);
      if (peek == TOKEN_COMMA) {
        if (last && !is_initializer) {
          parser_diag(1, parser, NULL,
                      "unexpected comma in block (this block is not an initializer)");
          return -1;
        }

        parser_consume_peeked(parser, NULL);

        is_initializer = 1;
      }
    }

    last = stmt;
  }
  if (parser_consume(parser, &token, TOKEN_RBRACE) < 0) {
    return -1;
  }

  into->last_stmt = last;

  if (is_initializer) {
    struct ast_stmt *stmt = into->stmt;

    into->stmt = calloc(1, sizeof(struct ast_stmt));
    into->stmt->type = AST_STMT_TYPE_EXPR;
    into->stmt->stmt.expr = calloc(1, sizeof(struct ast_expr));
    into->stmt->stmt.expr->type = AST_EXPR_TYPE_STRUCT_INIT;
    into->stmt->stmt.expr->parsed_ty.ty = AST_TYPE_TBD;
    into->stmt->stmt.expr->parsed_ty.oneof.array.element_ty = calloc(1, sizeof(struct ast_ty));
    into->stmt->stmt.expr->parsed_ty.oneof.array.element_ty->ty = AST_TYPE_TBD;

    // build expressions from statement list
    struct ast_expr *expr = into->stmt->stmt.expr;
    struct ast_expr_list *last_expr = NULL;
    size_t n = 0;
    while (stmt) {
      if (stmt->type != AST_STMT_TYPE_EXPR) {
        parser_diag(1, parser, NULL,
                    "only expressions are allowed in comma-separated initializer blocks");
        return -1;
      }

      struct ast_expr_list *node = calloc(1, sizeof(struct ast_expr_list));
      node->expr = stmt->stmt.expr;

      if (last_expr) {
        last_expr->next = node;
      } else {
        expr->expr.list = node;
      }

      last_expr = node;
      struct ast_stmt *old = stmt;
      stmt = stmt->next;

      free(old);

      ++n;
    }

    if (expr->expr.list) {
      expr->expr.list->num_elements = n;
    }

    return 0;
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
