#include "parse.h"

#include <ctype.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "compiler.h"
#include "lex.h"
#include "tokens.h"
#include "tokenstream.h"
#include "types.h"
#include "utility.h"

struct parser {
  struct lex_state *lexer;
  struct token peek;

  struct ast_program ast;

  int errors;
  int warnings;

  struct compiler *compiler;

  struct tokenstream *stream;

  // Mute diagnostics temporarily. Used when trying multiple parse paths to avoid diagnostics from
  // the failed paths.
  int mute_diags;

  struct parser_diag *diags;
  struct parser_diag *diags_tail;
};

struct parser_diag {
  char *message;
  struct lex_locator loc;
  enum ParserDiagSeverity severity;
  struct parser_diag *next;
};

static enum token_id parser_peek(struct parser *parser) __attribute__((warn_unused_result));

__attribute__((warn_unused_result)) static int parser_consume(struct parser *parser,
                                                              struct token *token,
                                                              enum token_id expected);

// Consume a token from the lexer without any checks. Use this when you know the token is correct,
// for example due to a previous peek. Without a previous peek, this function is unsafe.
static int parser_consume_peeked(struct parser *parser, struct token *token);

static struct ast_toplevel *parser_parse_toplevel(struct parser *parser);
static struct ast_toplevel *parser_parse_preproc(struct parser *parser);

__attribute__((warn_unused_result)) static int parse_block(struct parser *parser,
                                                           struct ast_block *into);
static struct ast_stmt *parse_statement(struct parser *parser, int *ended_semi);
static struct ast_expr *parse_expression(struct parser *parser);
static struct ast_expr *parse_expression_inner(struct parser *parser, int min_prec);
static struct ast_expr *parse_factor(struct parser *parser);

static struct ast_ty parse_type(struct parser *parser);
static struct ast_range parse_range(struct parser *parser);

static int parse_braced_initializer(struct parser *parser, struct ast_expr *into);

static int parser_parse_struct_decl(struct parser *parser, struct ast_ty *into);
static int parser_parse_enum_decl(struct parser *parser, struct ast_ty *into);

static struct ast_expr *parser_parse_pattern_match(struct parser *parser);

static int binary_op(enum token_id token);
static int binary_op_prec(int token);

// Rewind the parser and clear out the lookahead (peek) token so the next peek uses the rewound
// location.
static void parser_rewind(struct parser *parser) {
  tokenstream_rewind(parser->stream);
  parser->peek.ident = TOKEN_UNKNOWN;
}

// Commit the parser's backtrack buffer.
static void parser_commit(struct parser *parser) {
  tokenstream_commit(parser->stream);
}

__attribute__((format(printf, 4, 5))) static void parser_diag(int fatal, struct parser *parser,
                                                              struct token *token, const char *msg,
                                                              ...) {
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

struct parser *new_parser(struct lex_state *lexer, struct compiler *compiler) {
  struct parser *result = calloc(1, sizeof(struct parser));
  result->lexer = lexer;
  result->compiler = compiler;
  result->stream = new_tokenstream(lexer);
  return result;
}

int parser_run(struct parser *parser) {
  lexer_locate(parser->lexer, &parser->ast.loc);

  struct ast_toplevel *last = NULL;
  while (!lexer_eof(parser->lexer)) {
    struct ast_toplevel *decl = parser_parse_toplevel(parser);
    if (!decl) {
      // EOF is OK in this context (means peek hit EOF)
      if (lexer_eof(parser->lexer)) {
        break;
      }
      return -1;
    }

    if (!parser->ast.decls) {
      parser->ast.decls = decl;
    } else {
      last->next = decl;
    }

    last = decl;
  }

  return 0;
}

struct ast_program *parser_get_ast(struct parser *parser) {
  return &parser->ast;
}

void destroy_parser(struct parser *parser) {
  free_ast(&parser->ast);
  destroy_tokenstream(parser->stream);
  free(parser);
}

// peeks the next token in the stream, without skipping over newlines
static enum token_id parser_peek_with_nl(struct parser *parser) {
  // Eat comments - but in the future we will want to carry them with the AST
  while (parser->peek.ident == TOKEN_UNKNOWN || parser->peek.ident == TOKEN_COMMENTLINE ||
         parser->peek.ident == TOKEN_COMMENTLONG) {
    parser->peek.ident = TOKEN_UNKNOWN;
    int rc = tokenstream_next_token(parser->stream, &parser->peek);
    if (rc < 0) {
      return TOKEN_UNKNOWN;
    } else if (parser->peek.ident == TOKEN_UNKNOWN) {
      return TOKEN_UNKNOWN;
    }
  }

  return parser->peek.ident;
}

// peeks the next token in the stream, skipping over newlines
static enum token_id parser_peek(struct parser *parser) {
  enum token_id peeked = TOKEN_UNKNOWN;
  do {
    peeked = parser_peek_with_nl(parser);
    if (peeked == TOKEN_NEWLINE) {
      // eat it.
      parser->peek.ident = TOKEN_UNKNOWN;
    }
  } while (peeked == TOKEN_NEWLINE);

  return peeked;
}

static int parser_consume(struct parser *parser, struct token *token, enum token_id expected) {
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

static int parser_consume_peeked(struct parser *parser, struct token *token) {
  return parser_consume(parser, token, parser->peek.ident);
}

/**
 * Parse a variable declaration - for use in function parameter lists
 */
static struct ast_vdecl *parse_parse_vdecl(struct parser *parser) {
  struct ast_ty ty = parse_type(parser);
  if (type_is_error(&ty)) {
    return NULL;
  }

  struct ast_vdecl *vdecl = calloc(1, sizeof(struct ast_vdecl));
  vdecl->ty = ty;

  struct token token;
  if (parser_consume(parser, &token, TOKEN_IDENTIFIER) < 0) {
    free(vdecl);
    return NULL;
  }
  vdecl->ident = token;

  return vdecl;
}

static struct ast_toplevel *parser_parse_tydecl(struct parser *parser) {
  struct ast_toplevel *decl = calloc(1, sizeof(struct ast_toplevel));
  decl->type = AST_DECL_TYPE_TYDECL;

  if (parser_consume(parser, &decl->tydecl.ident, TOKEN_IDENTIFIER) < 0) {
    free(decl);
    return NULL;
  }
  if (parser_consume(parser, NULL, TOKEN_ASSIGN) < 0) {
    free(decl);
    return NULL;
  }

  decl->tydecl.ty = parse_type(parser);
  strncpy(decl->tydecl.ty.name, decl->tydecl.ident.value.identv.ident, 256);

  if (parser_consume(parser, NULL, TOKEN_SEMI) < 0) {
    free(decl);
    return NULL;
  }
  return decl;
}

static struct ast_toplevel *parser_parse_toplevel(struct parser *parser) {
  struct token token;
  enum token_id peek = parser_peek(parser);
  if (peek == TOKEN_UNKNOWN) {
    parser_diag(1, parser, NULL, "unexpected EOF or other error in token stream");
  } else if (peek == TOKEN_EOF) {
    return NULL;
  }

  struct lex_locator loc;
  lexer_locate(parser->lexer, &loc);

  if (peek == TOKEN_POUND) {
    struct ast_toplevel *result = parser_parse_preproc(parser);
    result->loc = loc;
    return result;
  } else if (peek == TOKEN_KW_TYPE) {
    parser_consume_peeked(parser, NULL);
    struct ast_toplevel *result = parser_parse_tydecl(parser);
    result->loc = loc;
    return result;
  }

  uint64_t flags = 0;

  struct ast_toplevel *decl = calloc(1, sizeof(struct ast_toplevel));
  decl->loc = loc;
  struct ast_fdecl *fdecl = &decl->fdecl;
  struct ast_vdecl *vdecl = &decl->vdecl;

  /**
   * [vis] <ty> [mut] <name> [= <init-expr>];
   * [vis] fn <ret-ty> <name>([<decl>]*) [block]
   */

  if (parser_peek(parser) == TOKEN_KW_PUB) {
    parser_consume_peeked(parser, NULL);
    flags |= DECL_FLAG_PUB;
  }

  // impure
  if (parser_peek(parser) == TOKEN_KW_IMPURE) {
    parser_consume_peeked(parser, NULL);
    flags |= DECL_FLAG_IMPURE;
  }

  // fn or type
  if (parser_peek(parser) == TOKEN_KW_FN) {
    parser_consume_peeked(parser, NULL);
    decl->type = AST_DECL_TYPE_FDECL;
  } else {
    if (flags & DECL_FLAG_IMPURE) {
      parser_diag(1, parser, NULL, "only functions can be impure");
      free(decl);
      return NULL;
    }
    decl->type = AST_DECL_TYPE_VDECL;
  }

  struct ast_ty ty = parse_type(parser);
  if (decl->type == AST_DECL_TYPE_FDECL) {
    fdecl->retty = ty;
  } else {
    vdecl->ty = ty;
  }

  // mut?
  if (decl->type != AST_DECL_TYPE_FDECL) {
    if (parser_peek(parser) == TOKEN_KW_MUT) {
      parser_consume_peeked(parser, NULL);
      vdecl->flags |= DECL_FLAG_MUT;
    }
  }

  // name
  if (parser_consume(parser, &token, TOKEN_IDENTIFIER) < 0) {
    free(decl);
    return NULL;
  }
  if (decl->type == AST_DECL_TYPE_FDECL) {
    fdecl->ident = token;
  } else {
    vdecl->ident = token;
  }

  if (decl->type == AST_DECL_TYPE_FDECL) {
    if (parser_consume(parser, NULL, TOKEN_LPAREN) < 0) {
      free(decl);
      return NULL;
    }
    while (parser_peek(parser) != TOKEN_RPAREN) {
      if (parser_peek(parser) == TOKEN_ASTERISK) {
        parser_consume_peeked(parser, NULL);
        fdecl->flags |= DECL_FLAG_VARARG;
        break;
      }

      // parse decls
      struct ast_vdecl *param = parse_parse_vdecl(parser);
      if (!param) {
        free(decl);
        return NULL;
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
        parser_consume_peeked(parser, NULL);
      } else {
        break;
      }
    }
    if (parser_consume(parser, NULL, TOKEN_RPAREN) < 0) {
      free(decl);
      return NULL;
    }
  }

  peek = parser_peek(parser);
  if (decl->type == AST_DECL_TYPE_FDECL) {
    if (peek == TOKEN_LBRACE) {
      // full function definition
      fdecl->body = calloc(1, sizeof(struct ast_block));
      if (parse_block(parser, fdecl->body) < 0) {
        free(decl);
        return NULL;
      }
    } else {
      if (parser_consume(parser, &token, TOKEN_SEMI) < 0) {
        free(decl);
        return NULL;
      }
    }
  } else {
    if (peek == TOKEN_ASSIGN) {
      parser_consume_peeked(parser, &token);
      vdecl->init_expr = parse_expression(parser);
    }

    if (parser_consume(parser, &token, TOKEN_SEMI) < 0) {
      free(decl);
      return NULL;
    }
  }

  if (decl->type == AST_DECL_TYPE_FDECL) {
    fdecl->flags |= flags;
  } else {
    vdecl->flags |= flags;
  }

  return decl;
}

static struct ast_toplevel *parser_parse_preproc(struct parser *parser) {
  struct ast_toplevel *decl = calloc(1, sizeof(struct ast_toplevel));
  decl->type = AST_DECL_TYPE_PREPROC;

  struct token token;

  if (parser_consume(parser, NULL, TOKEN_POUND) < 0) {
    free(decl);
    return NULL;
  }

  // what's next?
  enum token_id peek = parser_peek_with_nl(parser);
  switch (peek) {
    case TOKEN_INTEGER:
      // line number update - <int> <filename>
      if (parser_consume(parser, &token, TOKEN_INTEGER) < 0) {
        free(decl);
        return NULL;
      }
      size_t new_line = token.value.intv.val;

      if (parser_consume(parser, &token, TOKEN_STRING) < 0) {
        free(decl);
        return NULL;
      }
      // const char *new_file = token.value.strv.s;

      struct lex_locator loc;
      lexer_locate(parser->lexer, &loc);
      loc.line = new_line - 1;  // references the next line
      // TODO: sort this business out
      // strncpy(loc.file, new_file, 256);
      lexer_update_loc(parser->lexer, &loc);

      // consume the flags (if any) -- TODO: use the flags
      while (parser_peek_with_nl(parser) == TOKEN_INTEGER) {
        parser_consume_peeked(parser, NULL);
      }

      break;

    default:
      // unknown decl, just eat it
      break;
  }

  // eat whatever remains in the preprocessor definition
  while (1) {
    peek = parser_peek_with_nl(parser);
    if (peek == TOKEN_NEWLINE) {
      break;
    }

    parser_consume_peeked(parser, NULL);
  }

  if (parser_consume(parser, NULL, TOKEN_NEWLINE) < 0) {
    free(decl);
    return NULL;
  }

  return decl;
}

static int parse_block(struct parser *parser, struct ast_block *into) {
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
    if (last) {
      last->next = stmt;
    } else {
      into->stmt = stmt;
    }

    last = stmt;
  }
  if (parser_consume(parser, &token, TOKEN_RBRACE) < 0) {
    return -1;
  }

  if (ended_semi || !last || last->type != AST_STMT_TYPE_EXPR) {
    // add a void yielding expression
    struct ast_stmt *stmt = calloc(1, sizeof(struct ast_stmt));
    stmt->type = AST_STMT_TYPE_EXPR;
    stmt->expr = calloc(1, sizeof(struct ast_expr));
    stmt->expr->type = AST_EXPR_TYPE_VOID;
    if (last) {
      last->next = stmt;
    } else {
      into->stmt = stmt;
    }
  }

  return 0;
}

static struct ast_stmt *parse_statement(struct parser *parser, int *ended_semi) {
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
        result->let.flags |= DECL_FLAG_MUT;
      }
      struct ast_ty ty = type_tbd();
      if (parser_peek(parser) != TOKEN_IDENTIFIER) {
        ty = parse_type(parser);
      }
      if (parser_consume(parser, &token, TOKEN_IDENTIFIER) < 0) {
        free(result);
        return NULL;
      }
      result->type = AST_STMT_TYPE_LET;
      result->let.ident = token;
      if (parser_consume(parser, NULL, TOKEN_ASSIGN) < 0) {
        free(result);
        return NULL;
      }
      result->let.init_expr = parse_expression(parser);
      result->let.ty = ty;
      break;

    case TOKEN_KW_ITER: {
      parser_consume_peeked(parser, NULL);
      result->type = AST_STMT_TYPE_ITER;
      result->iter.range = parse_range(parser);
      if (parser_consume(parser, &token, TOKEN_IDENTIFIER) < 0) {
        free(result);
        return NULL;
      }
      result->iter.index.ident = token;
      if (parse_block(parser, &result->iter.block) < 0) {
        free(result);
        return NULL;
      }
    } break;

    case TOKEN_KW_STORE: {
      parser_consume_peeked(parser, NULL);
      result->type = AST_STMT_TYPE_STORE;
      result->store.lhs = parse_factor(parser);
      result->store.rhs = parse_expression(parser);
    } break;

    case TOKEN_KW_RETURN:
      parser_consume_peeked(parser, NULL);
      result->type = AST_STMT_TYPE_RETURN;
      result->expr = parse_expression(parser);
      break;

    case TOKEN_KW_DEFER:
      parser_consume_peeked(parser, NULL);
      result->type = AST_STMT_TYPE_DEFER;
      result->expr = parse_expression(parser);
      break;

    default:
      // it's actually an expression
      result->expr = parse_expression(parser);
      result->type = AST_STMT_TYPE_EXPR;
  }

  // must end in either semicolon or rbrace
  enum token_id peek = parser_peek(parser);
  if (peek != TOKEN_RBRACE) {
    if (parser_consume(parser, &token, TOKEN_SEMI) < 0) {
      free(result);
      return NULL;
    }
    *ended_semi = 1;
  }

  tokenstream_commit(parser->stream);
  return result;
}

// Parse comma-separated expressions.
static struct ast_expr_list *parse_expression_list(struct parser *parser, enum token_id terminator,
                                                   int factor_only) {
  struct ast_expr_list *result = NULL;
  size_t n = 0;
  while (parser_peek(parser) != terminator) {
    struct ast_expr_list *node = calloc(1, sizeof(struct ast_expr_list));
    node->expr = factor_only ? parse_factor(parser) : parse_expression(parser);

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
      parser_consume_peeked(parser, NULL);
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
  return parse_expression_inner(parser, 0);
}

static struct ast_expr *parse_expression_inner(struct parser *parser, int min_prec) {
  struct ast_expr *left = parse_factor(parser);

  while (1) {
    enum token_id peek = parser_peek(parser);
    if (peek == TOKEN_UNKNOWN) {
      parser_diag(1, parser, NULL, "unexpected lexer token in expression");
    }

    int binop = binary_op(peek);
    if (binop < 0) {
      break;
    }

    int prec = binary_op_prec(binop);
    if (prec < min_prec) {
      break;
    }

    parser_consume_peeked(parser, NULL);

    struct ast_expr *new_left = calloc(1, sizeof(struct ast_expr));
    new_left->type = AST_EXPR_TYPE_BINARY;
    new_left->binary.op = binop;
    new_left->binary.lhs = left;
    new_left->binary.rhs = parse_expression_inner(parser, prec + 1);
    left = new_left;
  }

  return left;
}

static struct ast_expr *parse_factor(struct parser *parser) {
  struct ast_expr *result = calloc(1, sizeof(struct ast_expr));
  result->ty.ty = AST_TYPE_TBD;

  struct token token;

  lexer_locate(parser->lexer, &result->loc);
  enum token_id peek = parser_peek(parser);
  switch (peek) {
    // unary expression (higher precedence than binary operators)
    case TOKEN_MINUS:
    case TOKEN_TILDE:
    case TOKEN_NOT:
      parser_consume_peeked(parser, &token);
      result->type = AST_EXPR_TYPE_UNARY;
      result->unary.op = peek == TOKEN_MINUS   ? AST_UNARY_OP_NEG
                         : peek == TOKEN_TILDE ? AST_UNARY_OP_COMP
                                               : AST_UNARY_OP_NOT;
      result->unary.expr = parse_factor(parser);
      break;

    // POD constants
    case TOKEN_INTEGER:
    case TOKEN_STRING:
    case TOKEN_CHAR:
    case TOKEN_FLOAT:
      parser_consume_peeked(parser, &token);
      result->type = AST_EXPR_TYPE_CONSTANT;
      result->constant.constant = token;

      // constants have a fully resolved type immediately
      if (peek == TOKEN_INTEGER) {
        result->ty.ty = AST_TYPE_INTEGER;
        result->ty.flags |= TYPE_FLAG_CONSTANT;
        result->ty.integer.is_signed = 1;
        if (token.value.intv.val == 0) {
          result->ty.integer.width = 1;
        } else {
          // width is the number of bits required to represent the value
          // also, constants are all positive, so add one bit for sign
          result->ty.integer.width =
              (8 * sizeof(token.value.intv.val) - (uint64_t)__builtin_clzll(token.value.intv.val)) +
              1;
        }
      } else if (peek == TOKEN_STRING) {
        result->ty.ty = AST_TYPE_STRING;
      } else if (peek == TOKEN_CHAR) {
        result->ty.ty = AST_TYPE_INTEGER;
        result->ty.integer.is_signed = 1;
        result->ty.integer.width = 8;
      } else if (peek == TOKEN_FLOAT) {
        result->ty.ty = AST_TYPE_FLOAT;
      }

      break;

    // array literals <ty> { <expr>, <expr>, ... }
    case TOKEN_TY_CHAR:
    case TOKEN_TY_FLOAT:
    case TOKEN_TY_FVEC:
    case TOKEN_TY_SIGNED:
    case TOKEN_TY_UNSIGNED:
    case TOKEN_TY_STR:
    case TOKEN_TY_VOID: {
      if (parse_braced_initializer(parser, result) < 0) {
        free(result);
        return NULL;
      }
    } break;

    // struct literals struct <ty> { <expr>, ... }
    case TOKEN_KW_STRUCT: {
      parser_consume_peeked(parser, NULL);
      if (parse_braced_initializer(parser, result) < 0) {
        free(result);
        return NULL;
      }

      // swap type for future passes (braced initializers are arrays by default)
      result->type = AST_EXPR_TYPE_STRUCT_INIT;
    } break;

    // vec literals
    case TOKEN_LT: {
      parser_consume_peeked(parser, NULL);
      result->type = AST_EXPR_TYPE_CONSTANT;
      result->ty.ty = AST_TYPE_FVEC;
      result->list = parse_expression_list(parser, TOKEN_GT, 1);
      if (!result->list) {
        free(result);
        parser_diag(1, parser, NULL, "failed to parse expression list for vector initializer");
        return NULL;
      }
      result->ty.fvec.width = result->list->num_elements;
      if (parser_consume(parser, NULL, TOKEN_GT) < 0) {
        free(result);
        return NULL;
      }
    } break;

      // identifiers (deref, function call, index)
    case TOKEN_IDENTIFIER: {
      parser_consume_peeked(parser, &token);

      enum token_id next = parser_peek(parser);
      if (next == TOKEN_UNKNOWN) {
        parser_diag(1, parser, NULL, "unexpected lexer token in factor");
      } else if (next == TOKEN_PERIOD) {
        parser_consume_peeked(parser, NULL);

        result->type = AST_EXPR_TYPE_DEREF;
        result->deref.ident = token;
        if (parser_consume(parser, &result->deref.field, TOKEN_IDENTIFIER) < 0) {
          free(result);
          return NULL;
        }
      } else if (next == TOKEN_LBRACKET) {
        parser_consume_peeked(parser, NULL);
        result->type = AST_EXPR_TYPE_ARRAY_INDEX;
        result->array_index.ident = token;
        result->array_index.index = parse_expression(parser);
        if (parser_consume(parser, NULL, TOKEN_RBRACKET) < 0) {
          free(result);
          return NULL;
        }
      } else if (next == TOKEN_LPAREN) {
        parser_consume_peeked(parser, NULL);
        result->type = AST_EXPR_TYPE_CALL;
        result->call.ident = token;
        result->call.args = parse_expression_list(parser, TOKEN_RPAREN, 0);
        if (parser_consume(parser, NULL, TOKEN_RPAREN) < 0) {
          free(result);
          return NULL;
        }
      } else if (next == TOKEN_ASSIGN) {
        parser_consume_peeked(parser, NULL);
        result->type = AST_EXPR_TYPE_ASSIGN;
        result->assign.ident = token;
        result->assign.expr = parse_expression(parser);
      } else if (next == TOKEN_COLONCOLON) {
        parser_consume_peeked(parser, NULL);
        result->type = AST_EXPR_TYPE_ENUM_INIT;
        result->enum_init.enum_ty_name = token;

        if (parser_consume(parser, &result->enum_init.enum_val_name, TOKEN_IDENTIFIER) < 0) {
          free(result);
          return NULL;
        }

        if (parser_peek(parser) == TOKEN_LPAREN) {
          result->enum_init.inner = parse_factor(parser);
        } else {
          result->enum_init.inner = NULL;
        }
      } else {
        result->type = AST_EXPR_TYPE_VARIABLE;
        result->variable.ident = token;
      }
    } break;

    // sub-expressions
    case TOKEN_LPAREN:
      free(result);
      parser_consume_peeked(parser, NULL);
      result = parse_expression(parser);
      if (parser_consume(parser, NULL, TOKEN_RPAREN) < 0) {
        free(result);
        return NULL;
      }
      break;

      // sub-blocks
    case TOKEN_LBRACE:
      result->type = AST_EXPR_TYPE_BLOCK;
      if (parse_block(parser, &result->block) < 0) {
        free(result);
        return NULL;
      }
      break;

      // type casts
    case TOKEN_KW_AS:
      // as <ty> <expr>
      parser_consume_peeked(parser, NULL);
      result->type = AST_EXPR_TYPE_CAST;
      result->cast.ty = parse_type(parser);
      result->cast.expr = parse_factor(parser);
      break;

      // ptr to value
    case TOKEN_KW_REF:
      // ref <expr>
      parser_consume_peeked(parser, NULL);
      result->type = AST_EXPR_TYPE_REF;
      result->ref.expr = parse_factor(parser);
      break;

      // deref ptr
    case TOKEN_KW_LOAD:
      // load <expr>
      parser_consume_peeked(parser, NULL);
      result->type = AST_EXPR_TYPE_LOAD;
      result->load.expr = parse_factor(parser);
      break;

    // if expressions (which evaluate to a value) or statements (no value)
    case TOKEN_KW_IF:
      parser_consume_peeked(parser, NULL);
      result->type = AST_EXPR_TYPE_IF;
      result->if_expr.cond = parse_expression(parser);
      if (parse_block(parser, &result->if_expr.then_block) < 0) {
        free(result);
        return NULL;
      }
      if (parser_peek(parser) == TOKEN_KW_ELSE) {
        result->if_expr.has_else = 1;
        parser_consume_peeked(parser, NULL);
        if (parse_block(parser, &result->if_expr.else_block) < 0) {
          free(result);
          return NULL;
        }
      } else {
        result->if_expr.has_else = 0;
      }
      break;

    // match expressions
    case TOKEN_KW_MATCH:
      parser_consume_peeked(parser, NULL);
      result->type = AST_EXPR_TYPE_MATCH;
      result->match.expr = parse_expression(parser);
      if (parser_consume(parser, NULL, TOKEN_LBRACE) < 0) {
        free(result);
        return NULL;
      }
      while (parser_peek(parser) != TOKEN_RBRACE) {
        struct ast_expr_match_arm *arm = calloc(1, sizeof(struct ast_expr_match_arm));
        int is_otherwise = 0;

        tokenstream_mark(parser->stream);
        parser->mute_diags = 1;
        arm->pattern = parser_parse_pattern_match(parser);
        parser->mute_diags = 0;
        if (!arm->pattern) {
          parser_rewind(parser);

          if (parser_peek(parser) == TOKEN_UNDER) {
            parser_consume_peeked(parser, NULL);
            arm->pattern = NULL;
            is_otherwise = 1;
          } else {
            arm->pattern = parse_expression(parser);
          }
        } else {
          parser_commit(parser);
        }

        if (parser_consume(parser, NULL, TOKEN_INTO) < 0) {
          free(result);
          return NULL;
        }
        arm->expr = parse_expression(parser);

        if (is_otherwise) {
          if (result->match.otherwise) {
            parser_diag(1, parser, &parser->peek, "multiple otherwise arms in match expression");
          }
          result->match.otherwise = arm;
        } else {
          if (!result->match.arms) {
            result->match.arms = arm;
          } else {
            struct ast_expr_match_arm *last = result->match.arms;
            while (last->next) {
              last = last->next;
            }

            last->next = arm;
          }

          // TODO: need a semantic analysis pass to yell if there's no arms!
          result->match.num_arms++;
        }
      }
      if (parser_consume(parser, NULL, TOKEN_RBRACE) < 0) {
        free(result);
        return NULL;
      }
      break;

    case TOKEN_KW_NIL:
      parser_consume_peeked(parser, NULL);
      result->type = AST_EXPR_TYPE_NIL;
      break;

    default:
      parser_diag(1, parser, &parser->peek, "unknown token %s when parsing factor\n",
                  token_id_to_string(peek));
  }

  return result;
}

static struct ast_ty parse_type(struct parser *parser) {
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
      tmplty->template.outer = calloc(1, sizeof(struct ast_ty));
      memcpy(tmplty->template.outer, &result, sizeof(struct ast_ty));

      struct ast_template_ty *inner_prev = NULL;

      parser_consume_peeked(parser, NULL);
      while (parser_peek(parser) != TOKEN_GT) {
        struct ast_template_ty *inner_ty = calloc(1, sizeof(struct ast_template_ty));
        inner_ty->resolved = parse_type(parser);
        inner_ty->is_resolved = 1;

        if (inner_prev == NULL) {
          tmplty->template.inners = inner_ty;
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
  } else if (peek == TOKEN_KW_STRUCT) {
    parser_consume_peeked(parser, NULL);

    if (parser_parse_struct_decl(parser, &result) < 0) {
      result.ty = AST_TYPE_ERROR;
      return result;
    }
  } else if (peek == TOKEN_KW_ENUM) {
    parser_consume_peeked(parser, NULL);

    if (parser_parse_enum_decl(parser, &result) < 0) {
      result.ty = AST_TYPE_ERROR;
      return result;
    }
  } else {
    parser_diag(1, parser, &parser->peek, "unexpected token of type %s when parsing type\n",
                token_id_to_string(peek));
  }

  if (parser_peek(parser) == TOKEN_ASTERISK) {
    parser_consume_peeked(parser, NULL);
    result.flags |= TYPE_FLAG_PTR;
  }

  if (parser_peek(parser) == TOKEN_LBRACKET) {
    parser_consume_peeked(parser, NULL);
    struct ast_ty *element_ty = calloc(1, sizeof(struct ast_ty));
    memcpy(element_ty, &result, sizeof(struct ast_ty));
    result.array.element_ty = element_ty;
    if (parser_consume(parser, &token, TOKEN_INTEGER) < 0) {
      result.ty = AST_TYPE_ERROR;
      return result;
    }
    result.array.width = token.value.tyv.dimension;
    if (parser_consume(parser, NULL, TOKEN_RBRACKET) < 0) {
      result.ty = AST_TYPE_ERROR;
      return result;
    }

    result.ty = AST_TYPE_ARRAY;
  }

  return result;
}

static int binary_op(enum token_id token) {
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
    case TOKEN_LSHIFT:
      return AST_BINARY_OP_LSHIFT;
    case TOKEN_RSHIFT:
      return AST_BINARY_OP_RSHIFT;
    case TOKEN_EQUALS:
      return AST_BINARY_OP_EQUAL;
    case TOKEN_NE:
      return AST_BINARY_OP_NOT_EQUAL;
    case TOKEN_LT:
      return AST_BINARY_OP_LT;
    case TOKEN_LTE:
      return AST_BINARY_OP_LTE;
    case TOKEN_GT:
      return AST_BINARY_OP_GT;
    case TOKEN_GTE:
      return AST_BINARY_OP_GTE;
    case TOKEN_OR:
      return AST_BINARY_OP_LOGICAL_OR;
    case TOKEN_AND:
      return AST_BINARY_OP_LOGICAL_AND;
    default:
      return -1;
  }
}

static struct ast_expr *wrap_cast(struct ast_expr *expr, struct ast_ty *ty) {
  if (!expr) {
    return NULL;
  }

  struct ast_expr *result = calloc(1, sizeof(struct ast_expr));
  result->type = AST_EXPR_TYPE_CAST;
  result->cast.expr = expr;
  result->cast.ty = *ty;
  return result;
}

static struct ast_range parse_range(struct parser *parser) {
  struct ast_range result;

  // ranges are all i64s (for now...)
  struct ast_ty ty;
  memset(&ty, 0, sizeof(struct ast_ty));
  ty.ty = AST_TYPE_INTEGER;
  ty.integer.is_signed = 1;
  ty.integer.width = 64;

  result.start = wrap_cast(parse_expression(parser), &ty);
  if (parser_consume(parser, NULL, TOKEN_COLON) < 0) {
    // TODO: parse_range needs to be able to return an error
    result.start = NULL;
    return result;
  }
  result.end = wrap_cast(parse_expression(parser), &ty);

  if (parser_peek(parser) == TOKEN_COLON) {
    parser_consume_peeked(parser, NULL);
    result.step = wrap_cast(parse_expression(parser), &ty);
  } else {
    result.step = NULL;
  }

  return result;
}

static int binary_op_prec(int op) {
  switch (op) {
    case AST_BINARY_OP_LOGICAL_OR:
      return 5;
    case AST_BINARY_OP_LOGICAL_AND:
      return 10;
    case AST_BINARY_OP_BITOR:
      return 15;
    case AST_BINARY_OP_BITXOR:
      return 20;
    case AST_BINARY_OP_BITAND:
      return 25;
    case AST_BINARY_OP_EQUAL:
    case AST_BINARY_OP_NOT_EQUAL:
      return 30;
    case AST_BINARY_OP_LT:
    case AST_BINARY_OP_LTE:
    case AST_BINARY_OP_GT:
    case AST_BINARY_OP_GTE:
      return 35;
    case AST_BINARY_OP_LSHIFT:
    case AST_BINARY_OP_RSHIFT:
      return 40;
    case AST_BINARY_OP_ADD:
    case AST_BINARY_OP_SUB:
      return 45;
    case AST_BINARY_OP_MUL:
    case AST_BINARY_OP_DIV:
    case AST_BINARY_OP_MOD:
      return 50;
    default:
      fprintf(stderr, "returning lowest possible precedence for unknown binary op %d \n", op);
  }

  return 0;
}

static int parse_braced_initializer(struct parser *parser, struct ast_expr *into) {
  struct ast_ty element_ty = parse_type(parser);
  if (parser_consume(parser, NULL, TOKEN_LBRACE) < 0) {
    return -1;
  }
  into->type = AST_EXPR_TYPE_CONSTANT;
  into->ty.ty = AST_TYPE_ARRAY;
  into->ty.array.element_ty = calloc(1, sizeof(struct ast_ty));
  *into->ty.array.element_ty = element_ty;
  into->list = parse_expression_list(parser, TOKEN_RBRACE, 0);
  if (!into->list) {
    parser_diag(1, parser, &parser->peek, "braced initializer must have at least one element");
    free(into->ty.array.element_ty);
    return -1;
  }
  into->ty.array.width = into->list->num_elements;
  if (parser_consume(parser, NULL, TOKEN_RBRACE) < 0) {
    return -1;
  }
  return 0;
}

static int parser_parse_struct_decl(struct parser *parser, struct ast_ty *into) {
  if (parser_consume(parser, NULL, TOKEN_LBRACE) < 0) {
    return -1;
  }

  into->ty = AST_TYPE_STRUCT;

  struct ast_struct_field *last = into->structty.fields;

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
    field->ty = calloc(1, sizeof(struct ast_ty));
    *field->ty = field_ty;
    strncpy(field->name, token.value.identv.ident, 256);

    if (!last) {
      into->structty.fields = field;
      last = field;
    } else {
      last->next = field;
      last = field;
    }

    ++into->structty.num_fields;

    if (parser_consume(parser, NULL, TOKEN_SEMI) < 0) {
      return -1;
    }
  }

  if (!into->structty.fields) {
    parser_diag(1, parser, &parser->peek, "structs must have at least one field");
    return -1;
  }

  return parser_consume(parser, NULL, TOKEN_RBRACE);
}

int parser_parse_enum_decl(struct parser *parser, struct ast_ty *into) {
  struct token token;

  into->ty = AST_TYPE_ENUM;
  into->enumty.no_wrapped_fields = 1;

  enum token_id peek = parser_peek(parser);
  if (peek == TOKEN_LT) {
    parser_consume_peeked(parser, NULL);

    struct ast_template_ty *last = into->enumty.templates;

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
        into->enumty.templates = template;
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

  struct ast_enum_field *last = into->enumty.fields;
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
      field->inner = parse_type(parser);

      if (parser_consume(parser, NULL, TOKEN_RPAREN) < 0) {
        return -1;
      }

      into->enumty.no_wrapped_fields = 0;
    }

    if (last) {
      last->next = field;
      last = field;
    } else {
      into->enumty.fields = field;
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

static struct ast_expr *parser_parse_pattern_match(struct parser *parser) {
  struct ast_expr *result = calloc(1, sizeof(struct ast_expr));
  result->type = AST_EXPR_TYPE_PATTERN_MATCH;
  lexer_locate(parser->lexer, &result->loc);

  if (parser_consume(parser, &result->pattern_match.enum_name, TOKEN_IDENTIFIER) !=
      TOKEN_IDENTIFIER) {
    goto fail;
  }
  if (parser_consume(parser, NULL, TOKEN_COLONCOLON) != TOKEN_COLONCOLON) {
    goto fail;
  }
  if (parser_consume(parser, &result->pattern_match.name, TOKEN_IDENTIFIER) != TOKEN_IDENTIFIER) {
    goto fail;
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
      result->pattern_match.inner_vdecl->flags = DECL_FLAG_TEMPORARY;
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

struct parser_diag *parser_pop_diag(struct parser *parser) {
  struct parser_diag *diag = parser->diags;
  if (!diag) {
    return diag;
  }

  parser->diags = diag->next;
  return diag;
}

void parser_free_diag(struct parser_diag *diag) {
  if (diag) {
    free(diag->message);
    free(diag);
  }
}

struct lex_locator *parser_diag_loc(struct parser_diag *diag) {
  return &diag->loc;
}

const char *parser_diag_msg(struct parser_diag *diag) {
  return diag->message;
}

enum ParserDiagSeverity parser_diag_severity(struct parser_diag *diag) {
  return diag->severity;
}
