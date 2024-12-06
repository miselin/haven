#include "parse.h"

#include <malloc.h>
#include <setjmp.h>
#include <string.h>

#include "ast.h"
#include "lex.h"
#include "tokens.h"
#include "utility.h"

struct parser {
  struct lex_state *lexer;
  struct token peek;

  struct ast_program ast;

  jmp_buf errbuf;
};

static enum token_id parser_peek(struct parser *parser);

static int parser_consume(struct parser *parser, struct token *token, enum token_id expected);

static int parser_parse_toplevel(struct parser *parser);

static int parse_block(struct parser *parser, struct ast_block *into);
static struct ast_stmt *parse_statement(struct parser *parser, int *ended_semi);
static struct ast_expr *parse_expression(struct parser *parser);

static enum ast_ty parse_type(struct parser *parser);

static int binary_op(int token);

struct parser *new_parser(struct lex_state *lexer) {
  struct parser *result = calloc(1, sizeof(struct parser));
  result->lexer = lexer;
  return result;
}

int parser_run(struct parser *parser) {
  if (setjmp(parser->errbuf) != 0) {
    // -1 = EOF (or other weird lexer error)
    // -2 = unexpected token
    return -1;
  }

  while (!lexer_eof(parser->lexer)) {
    int rc = parser_parse_toplevel(parser);
    if (rc == 1) {
      // EOF
      break;
    } else if (rc < 0) {
      // EOF is OK in this context (means peek hit EOF)
      if (lexer_eof(parser->lexer)) {
        break;
      }
      return -1;
    }
  }

  return 0;
}

struct ast_program *parser_get_ast(struct parser *parser) {
  return &parser->ast;
}

void destroy_parser(struct parser *parser) {
  free(parser);
}

static enum token_id parser_peek(struct parser *parser) {
  if (parser->peek.ident == 0) {
    int rc = lexer_token(parser->lexer, &parser->peek);
    if (rc < 0) {
      return TOKEN_UNKNOWN;
    }
  }

  return parser->peek.ident;
}

static int parser_consume(struct parser *parser, struct token *token, enum token_id expected) {
  int rc = parser_peek(parser);
  if (rc < 0) {
    return rc;
  }

  if (parser->peek.ident <= 0) {
    fprintf(stderr, "parse: error in token stream (EOF?)\n");
    longjmp(parser->errbuf, -1);
  } else if (parser->peek.ident != expected) {
    fprintf(stderr, "parse: unexpected token %d, wanted %d\n", parser->peek.ident, expected);
    longjmp(parser->errbuf, -2);
  }

  fprintf(stderr, "consume: ");
  print_token(&parser->peek);
  fprintf(stderr, "\n");

  if (token) {
    memcpy(token, &parser->peek, sizeof(struct token));
  }

  parser->peek.ident = TOKEN_UNKNOWN;
  return 0;
}

/**
 * Parse a variable declaration - for use in function parameter lists
 */
static struct ast_vdecl *parse_parse_vdecl(struct parser *parser) {
  enum ast_ty ty = parse_type(parser);
  if (ty == AST_TYPE_ERROR) {
    return NULL;
  }
  parser_consume(parser, NULL, parser_peek(parser));

  struct ast_vdecl *vdecl = calloc(1, sizeof(struct ast_vdecl));
  vdecl->ty = ty;

  struct token token;
  parser_consume(parser, &token, TOKEN_IDENTIFIER);
  vdecl->ident = token;

  return vdecl;
}

static int parser_parse_toplevel(struct parser *parser) {
  struct token token;
  enum token_id tok = parser_peek(parser);
  if (tok == TOKEN_UNKNOWN) {
    return -1;
  } else if (tok == TOKEN_EOF) {
    return 1;
  }

  int flags = 0;

  struct ast_toplevel *decl = calloc(1, sizeof(struct ast_toplevel));
  struct ast_fdecl *fdecl = &decl->fdecl;
  struct ast_vdecl *vdecl = &decl->vdecl;

  /**
   * [vis] <ty> [mut] <name> [= <init-expr>];
   * [vis] fn <ret-ty> <name>([<decl>]*) [block]
   */

  if (parser_peek(parser) == TOKEN_KW_PUB) {
    parser_consume(parser, NULL, TOKEN_KW_PUB);
    flags |= DECL_FLAG_PUB;
  }

  // fn or type
  if (parser_peek(parser) == TOKEN_KW_FN) {
    parser_consume(parser, NULL, TOKEN_KW_FN);
    decl->is_fn = 1;
  }

  int peek = parser_peek(parser);
  enum ast_ty ty = parse_type(parser);
  if (ty == AST_TYPE_ERROR) {
    return -1;
  }
  parser_consume(parser, NULL, peek);
  if (decl->is_fn) {
    fdecl->retty = ty;
  } else {
    vdecl->ty = ty;
  }

  // mut or name
  if (decl->is_fn) {
    parser_consume(parser, &token, TOKEN_IDENTIFIER);
    fdecl->ident = token;
  } else {
    if (parser_peek(parser) == TOKEN_KW_MUT) {
      parser_consume(parser, NULL, TOKEN_KW_MUT);
      vdecl->flags |= DECL_FLAG_MUT;
    }

    parser_consume(parser, &token, TOKEN_IDENTIFIER);
    vdecl->ident = token;
  }

  if (decl->is_fn) {
    parser_consume(parser, NULL, TOKEN_LPAREN);
    while (parser_peek(parser) != TOKEN_RPAREN) {
      if (parser_peek(parser) == TOKEN_ASTERISK) {
        parser_consume(parser, NULL, TOKEN_ASTERISK);
        fdecl->flags |= DECL_FLAG_VARARG;
        break;
      }

      // parse decls
      struct ast_vdecl *param = parse_parse_vdecl(parser);
      if (!param) {
        fprintf(stderr, "parse: error parsing function parameter\n");
        longjmp(parser->errbuf, -1);
      }

      param->flags |= DECL_FLAG_TEMPORARY;

      if (!fdecl->params) {
        fdecl->params = calloc(1, sizeof(struct ast_vdecl *));
        fdecl->params[0] = param;
      } else {
        size_t new_size = fdecl->num_params + 1;
        fdecl->params = realloc(fdecl->params, new_size * sizeof(struct ast_vdecl *));
        fdecl->params[new_size - 1] = param;
      }

      fdecl->num_params++;

      if (parser_peek(parser) == TOKEN_COMMA) {
        parser_consume(parser, NULL, TOKEN_COMMA);
      } else {
        break;
      }
    }
    parser_consume(parser, NULL, TOKEN_RPAREN);
  }

  peek = parser_peek(parser);
  if (decl->is_fn) {
    if (peek == TOKEN_LBRACE) {
      // full function definition
      fdecl->body = calloc(1, sizeof(struct ast_block));
      parse_block(parser, fdecl->body);
    } else {
      parser_consume(parser, &token, TOKEN_SEMI);
    }
  } else {
    if (peek == TOKEN_ASSIGN) {
      parser_consume(parser, &token, TOKEN_ASSIGN);
      vdecl->init_expr = parse_expression(parser);
    }

    parser_consume(parser, &token, TOKEN_SEMI);
  }

  if (decl->is_fn) {
    fdecl->flags |= flags;
  } else {
    vdecl->flags |= flags;
  }

  if (!parser->ast.decls) {
    parser->ast.decls = decl;
  } else {
    struct ast_toplevel *last = parser->ast.decls;
    while (last->next) {
      last = last->next;
    }

    last->next = decl;
  }

  return 0;
}

static int parse_block(struct parser *parser, struct ast_block *into) {
  struct ast_stmt *last = NULL;

  /**
   * { <stmt>* }
   */

  struct token token;
  parser_consume(parser, &token, TOKEN_LBRACE);
  int ended_semi = 0;
  while (1) {
    int peek = parser_peek(parser);
    if (peek == TOKEN_RBRACE) {
      break;
    } else {
      ended_semi = 0;
    }

    struct ast_stmt *stmt = parse_statement(parser, &ended_semi);
    if (last) {
      last->next = stmt;
    } else {
      into->stmt = stmt;
    }

    last = stmt;
  }
  parser_consume(parser, &token, TOKEN_RBRACE);

  if (ended_semi) {
    // add a void yielding expression
    struct ast_stmt *stmt = calloc(1, sizeof(struct ast_stmt));
    stmt->type = AST_STMT_TYPE_EXPR;
    stmt->expr = calloc(1, sizeof(struct ast_expr));
    stmt->expr->type = AST_EXPR_TYPE_VOID;
    last->next = stmt;
  }

  return 0;
}

static struct ast_stmt *parse_statement(struct parser *parser, int *ended_semi) {
  struct ast_stmt *result = calloc(1, sizeof(struct ast_stmt));
  struct token token;

  switch (parser_peek(parser)) {
    case TOKEN_KW_LET:
      // let <type> <name> = <expr>;
      parser_consume(parser, NULL, TOKEN_KW_LET);
      enum ast_ty ty = AST_TYPE_TBD;
      if (parser_peek(parser) != TOKEN_IDENTIFIER) {
        // TODO: parse out a type
      }
      parser_consume(parser, &token, TOKEN_IDENTIFIER);
      result->type = AST_STMT_TYPE_LET;
      result->let.ident = token;
      parser_consume(parser, NULL, TOKEN_ASSIGN);
      result->let.init_expr = parse_expression(parser);
      result->let.ty = ty;
      break;

    default:
      // it's actually an expression
      result->expr = parse_expression(parser);
      result->type = AST_STMT_TYPE_EXPR;
  }

  // must end in either semicolon or rbrace
  int peek = parser_peek(parser);
  if (peek != TOKEN_RBRACE) {
    parser_consume(parser, &token, TOKEN_SEMI);
    *ended_semi = 1;
  }
  return result;
}

// Parse comma-separated expressions.
static struct ast_expr_list *parse_expression_list(struct parser *parser,
                                                   enum token_id terminator) {
  struct ast_expr_list *result = NULL;
  size_t n = 0;
  while (parser_peek(parser) != terminator) {
    struct ast_expr_list *node = calloc(1, sizeof(struct ast_expr_list));
    node->expr = parse_expression(parser);

    if (!result) {
      result = node;
    } else {
      struct ast_expr_list *last = result;
      while (last->next) {
        last = last->next;
      }

      last->next = node;
    }

    ++n;

    if (parser_peek(parser) == TOKEN_COMMA) {
      parser_consume(parser, NULL, TOKEN_COMMA);
    } else {
      break;
    }
  }

  if (!result) {
    return NULL;
  }

  result->num_elements = n;
  return result;
}

static struct ast_expr *parse_expression(struct parser *parser) {
  struct ast_expr *result = calloc(1, sizeof(struct ast_expr));
  struct token token;

  int peek = parser_peek(parser);
  switch (peek) {
    case TOKEN_INTEGER:
    case TOKEN_STRING:
    case TOKEN_CHAR:
    case TOKEN_FLOAT:
      parser_consume(parser, &token, peek);
      result->type = AST_EXPR_TYPE_CONSTANT;
      result->constant.constant = token;

      // constants have a fully resolved type immediately
      if (peek == TOKEN_INTEGER) {
        result->ty = AST_TYPE_INTEGER;
      } else if (peek == TOKEN_STRING) {
        result->ty = AST_TYPE_STRING;
      } else if (peek == TOKEN_CHAR) {
        result->ty = AST_TYPE_CHAR;
      } else if (peek == TOKEN_FLOAT) {
        result->ty = AST_TYPE_FLOAT;
      }
      break;

    case TOKEN_LT: {
      parser_consume(parser, NULL, TOKEN_LT);
      result->type = AST_EXPR_TYPE_CONSTANT;
      result->ty = AST_TYPE_FVEC;
      result->list = parse_expression_list(parser, TOKEN_GT);
      parser_consume(parser, NULL, TOKEN_GT);
    } break;

    case TOKEN_IDENTIFIER:
      parser_consume(parser, &token, TOKEN_IDENTIFIER);

      if (parser_peek(parser) == TOKEN_PERIOD) {
        parser_consume(parser, NULL, TOKEN_PERIOD);

        result->type = AST_EXPR_TYPE_DEREF;
        result->deref.ident = token;
        parser_consume(parser, &result->deref.field, TOKEN_IDENTIFIER);
      } else if (parser_peek(parser) == TOKEN_LPAREN) {
        parser_consume(parser, NULL, TOKEN_LPAREN);
        result->type = AST_EXPR_TYPE_CALL;
        result->call.ident = token;
        result->call.args = parse_expression_list(parser, TOKEN_RPAREN);
        parser_consume(parser, NULL, TOKEN_RPAREN);
      } else {
        result->type = AST_EXPR_TYPE_VARIABLE;
        result->variable.ident = token;
      }
      break;

    case TOKEN_LPAREN:
      free(result);
      parser_consume(parser, &token, TOKEN_LPAREN);
      result = parse_expression(parser);
      parser_consume(parser, &token, TOKEN_RPAREN);
      break;

    case TOKEN_LBRACE:
      result->type = AST_EXPR_TYPE_BLOCK;
      parse_block(parser, &result->block);
      break;

    case TOKEN_PLUS:
    case TOKEN_MINUS:
    case TOKEN_ASTERISK:
    case TOKEN_FSLASH:
    case TOKEN_PERCENT:
    case TOKEN_BITOR:
    case TOKEN_BITAND:
    case TOKEN_BITXOR:
      parser_consume(parser, &token, peek);
      result->type = AST_EXPR_TYPE_BINARY;
      result->binary.op = binary_op(peek);
      result->binary.lhs = parse_expression(parser);
      result->binary.rhs = parse_expression(parser);
      break;

    case TOKEN_AND:
    case TOKEN_OR:
      parser_consume(parser, &token, peek);
      result->type = AST_EXPR_TYPE_LOGICAL;
      result->logical.op = peek == TOKEN_AND ? AST_LOGICAL_OP_AND : AST_LOGICAL_OP_OR;
      result->logical.lhs = parse_expression(parser);
      result->logical.rhs = parse_expression(parser);
      break;

    default:
      fprintf(stderr, "unknown token %d when parsing expression\n", peek);
      longjmp(parser->errbuf, -3);
  }

  return result;
}

static enum ast_ty parse_type(struct parser *parser) {
  int peek = parser_peek(parser);
  if (peek == TOKEN_TY_SIGNED || peek == TOKEN_TY_UNSIGNED) {
    return AST_TYPE_INTEGER;
  } else if (peek == TOKEN_TY_STR) {
    return AST_TYPE_STRING;
  } else if (peek == TOKEN_TY_CHAR) {
    return AST_TYPE_CHAR;
  } else if (peek == TOKEN_TY_FLOAT) {
    return AST_TYPE_FLOAT;
  } else if (peek == TOKEN_TY_FVEC) {
    return AST_TYPE_FVEC;
  } else if (peek == TOKEN_TY_VOID) {
    return AST_TYPE_VOID;
  }

  return AST_TYPE_ERROR;
}

static int binary_op(int token) {
  switch (token) {
    case TOKEN_PLUS:
      return AST_BINARY_OP_ADD;
    case TOKEN_MINUS:
      return AST_BINARY_OP_SUB;
    case TOKEN_ASTERISK:
      return AST_BINARY_OP_MUL;
    case TOKEN_FSLASH:
      return AST_BINARY_OP_DIV;
    case TOKEN_PERCENT:
      return AST_BINARY_OP_MOD;
    case TOKEN_BITOR:
      return AST_BINARY_OP_BITOR;
    case TOKEN_BITAND:
      return AST_BINARY_OP_BITAND;
    case TOKEN_BITXOR:
      return AST_BINARY_OP_BITXOR;
    default:
      return -1;
  }
}
