#include <stdlib.h>
#include <string.h>

#include "internal.h"
#include "parse.h"
#include "tokenstream.h"

struct ast_stmt *parse_statement(struct parser *parser, int *ended_semi) {
  struct ast_stmt *result = calloc(1, sizeof(struct ast_stmt));
  lexer_locate(parser->lexer, &result->loc);

  struct token token;
  memset(&token, 0, sizeof(struct token));

  switch (parser_peek(parser)) {
    case TOKEN_UNKNOWN:
      parser_diag(1, parser, NULL, "unexpected lexer token in statement");
      break;

    case TOKEN_KW_LET:
      // let [mut] <type>? <name> = <expr>;
      parser_consume_peeked(parser, NULL);
      if (parser_peek(parser) == TOKEN_KW_MUT) {
        parser_consume_peeked(parser, NULL);
        result->stmt.let.flags |= DECL_FLAG_MUT;
      }

      int is_typed = 1;

      // seek ahead a bit to see if it's typed or not
      if (parser_peek(parser) == TOKEN_IDENTIFIER) {
        parser_mark(parser);
        parser_consume_peeked(parser, NULL);
        if (parser_peek(parser) == TOKEN_ASSIGN) {
          is_typed = 0;
        }
        parser_rewind(parser);
      }

      struct ast_ty ty = is_typed ? parse_type(parser) : type_tbd();

      // var name
      if (parser_consume(parser, &token, TOKEN_IDENTIFIER) < 0) {
        free(result);
        return NULL;
      }
      result->type = AST_STMT_TYPE_LET;
      result->stmt.let.ident = token;
      if (parser_consume(parser, NULL, TOKEN_ASSIGN) < 0) {
        free(result);
        return NULL;
      }
      result->stmt.let.init_expr = parse_expression(parser);
      result->stmt.let.parser_ty = ty;
      if (!result->stmt.let.init_expr) {
        free(result);
        return NULL;
      }
      break;

    case TOKEN_KW_ITER: {
      parser_consume_peeked(parser, NULL);
      result->type = AST_STMT_TYPE_ITER;
      result->stmt.iter.range = parse_range(parser);
      if (parser_consume(parser, &token, TOKEN_IDENTIFIER) < 0) {
        free(result);
        return NULL;
      }
      result->stmt.iter.index.ident = token;
      if (parse_block(parser, &result->stmt.iter.block) < 0) {
        free(result);
        return NULL;
      }
    } break;

    case TOKEN_KW_STORE: {
      parser_consume_peeked(parser, NULL);
      result->type = AST_STMT_TYPE_STORE;
      result->stmt.store.lhs = parse_factor(parser);
      result->stmt.store.rhs = parse_expression(parser);
      if (!(result->stmt.store.lhs && result->stmt.store.rhs)) {
        free(result);
        return NULL;
      }
    } break;

    case TOKEN_KW_RETURN:
      parser_consume_peeked(parser, NULL);
      result->type = AST_STMT_TYPE_RETURN;
      parser_mark(parser);
      parser->mute_diags = 1;
      result->stmt.expr = parse_expression(parser);
      parser->mute_diags = 0;
      if (!result->stmt.expr) {
        // probably a void return
        parser_rewind(parser);
      } else {
        parser_commit(parser);
      }
      break;

    case TOKEN_KW_DEFER:
      parser_consume_peeked(parser, NULL);
      result->type = AST_STMT_TYPE_DEFER;
      result->stmt.expr = parse_expression(parser);
      if (!result->stmt.expr) {
        free(result);
        return NULL;
      }
      break;

    case TOKEN_KW_WHILE:
      parser_consume_peeked(parser, NULL);
      result->type = AST_STMT_TYPE_WHILE;
      result->stmt.while_stmt.cond = parse_expression(parser);
      if (!result->stmt.while_stmt.cond) {
        free(result);
        return NULL;
      }
      if (parse_block(parser, &result->stmt.while_stmt.block) < 0) {
        free(result);
        return NULL;
      }
      break;

    case TOKEN_KW_UNTIL: {
      // until is basically sugar for a while loop, but with an inverted condition
      parser_consume_peeked(parser, NULL);
      result->type = AST_STMT_TYPE_WHILE;
      struct ast_expr *inner = parse_expression(parser);
      if (!inner) {
        free(result);
        return NULL;
      }
      result->stmt.while_stmt.cond = (struct ast_expr *)calloc(1, sizeof(struct ast_expr));
      lexer_locate(parser->lexer, &result->stmt.while_stmt.cond->loc);
      result->stmt.while_stmt.cond->type = AST_EXPR_TYPE_UNARY;
      result->stmt.while_stmt.cond->expr.unary.op = AST_UNARY_OP_NOT;
      result->stmt.while_stmt.cond->expr.unary.expr = inner;
      if (parse_block(parser, &result->stmt.while_stmt.block) < 0) {
        free(result);
        return NULL;
      }
    } break;

    case TOKEN_KW_BREAK:
      parser_consume_peeked(parser, NULL);
      result->type = AST_STMT_TYPE_BREAK;
      break;

    case TOKEN_KW_CONTINUE:
      parser_consume_peeked(parser, NULL);
      result->type = AST_STMT_TYPE_CONTINUE;
      break;

    default:
      // it's actually an expression
      result->stmt.expr = parse_expression(parser);
      result->type = AST_STMT_TYPE_EXPR;
      if (!result->stmt.expr) {
        free(result);
        return NULL;
      }
  }

  // must end in semicolon, or rbrace
  enum token_id peek = parser_peek(parser);

  // <stmt>;
  if (peek != TOKEN_RBRACE) {
    if (result->type == AST_STMT_TYPE_EXPR && peek == TOKEN_COMMA) {
      // This is actually probably part of an initializer, so we need to not consume the comma
      // to allow the caller to discover that and switch parsing mode
      *ended_semi = 0;
    } else {
      if (parser_consume(parser, &token, TOKEN_SEMI) < 0) {
        free(result);
        return NULL;
      }
      *ended_semi = 1;
    }
  }

  tokenstream_commit(parser->stream);
  return result;
}
