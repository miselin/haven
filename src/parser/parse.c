#include "parse.h"

#include <ctype.h>
#include <malloc.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "lex.h"
#include "tokens.h"
#include "types.h"
#include "utility.h"

struct parser {
  struct lex_state *lexer;
  struct token peek;

  struct ast_program ast;

  jmp_buf errbuf;

  int errors;
  int warnings;
};

static enum token_id parser_peek(struct parser *parser);

static int parser_consume(struct parser *parser, struct token *token, enum token_id expected);

static int parser_parse_toplevel(struct parser *parser);
static int parser_parse_preproc(struct parser *parser);

static int parse_block(struct parser *parser, struct ast_block *into);
static struct ast_stmt *parse_statement(struct parser *parser, int *ended_semi);
static struct ast_expr *parse_expression(struct parser *parser);
static struct ast_expr *parse_expression_inner(struct parser *parser, int min_prec);
static struct ast_expr *parse_factor(struct parser *parser);

static struct ast_ty parse_type(struct parser *parser);
static struct ast_range parse_range(struct parser *parser);

static int deref_to_index(const char *deref);

static int binary_op(int token);
static int binary_op_prec(int token);

static void parser_diag(int fatal, struct parser *parser, struct token *token, const char *msg,
                        ...) {
  if (token) {
    char locbuf[256] = {0};
    lexer_locate_str(parser->lexer, locbuf, 256);
    fprintf(stderr, "%s: ", locbuf);
  } else {
    fprintf(stderr, "???: ");
  }

  va_list args;
  va_start(args, msg);
  vfprintf(stderr, msg, args);
  va_end(args);

  if (fatal) {
    ++parser->errors;

    longjmp(parser->errbuf, -1);
  } else {
    ++parser->warnings;
  }
}

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
  free_ast(&parser->ast);
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
    parser_diag(1, parser, NULL, "unexpected EOF or other error in token stream\n");
    return -1;
  } else if (parser->peek.ident != expected) {
    parser_diag(1, parser, &parser->peek, "unexpected token %s, wanted %s\n",
                token_id_to_string(parser->peek.ident), token_id_to_string(expected));
    return -1;
  }

  // fprintf(stderr, "consume: %s\n", token_id_to_string(parser->peek.ident));

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

static int parser_parse_toplevel(struct parser *parser) {
  struct token token;
  enum token_id peek = parser_peek(parser);
  if (peek == TOKEN_UNKNOWN) {
    return -1;
  } else if (peek == TOKEN_EOF) {
    return 1;
  }

  if (peek == TOKEN_POUND) {
    return parser_parse_preproc(parser);
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

  struct ast_ty ty = parse_type(parser);
  if (type_is_error(&ty)) {
    free(decl);
    return -1;
  }
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
        free(decl);
        return -1;
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

static int parser_parse_preproc(struct parser *parser) {
  struct token token;

  parser_consume(parser, NULL, TOKEN_POUND);

  // what's next?
  enum token_id peek = parser_peek(parser);
  switch (peek) {
    case TOKEN_INTEGER:
      // line number update - <int> <filename>
      parser_consume(parser, &token, TOKEN_INTEGER);
      size_t new_line = token.value.intv.val;

      parser_consume(parser, &token, TOKEN_STRING);
      const char *new_file = token.value.strv.s;

      struct lex_locator loc;
      lexer_locate(parser->lexer, &loc);
      loc.line = new_line - 1;  // references the next line
      strncpy(loc.file, new_file, 256);
      lexer_update_loc(parser->lexer, &loc);

      // consume the flags (if any) -- TODO: use the flags
      while (parser_peek(parser) == TOKEN_INTEGER) {
        parser_consume(parser, NULL, TOKEN_INTEGER);
      }

      break;

    default:
      parser_diag(1, parser, NULL, "unexpected token %s in preprocessor line\n",
                  token_id_to_string(peek));
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
      // let [mut] <type>? <name> = <expr>;
      parser_consume(parser, NULL, TOKEN_KW_LET);
      if (parser_peek(parser) == TOKEN_KW_MUT) {
        parser_consume(parser, NULL, TOKEN_KW_MUT);
        result->let.flags |= DECL_FLAG_MUT;
      }
      struct ast_ty ty = type_tbd();
      if (parser_peek(parser) != TOKEN_IDENTIFIER) {
        ty = parse_type(parser);
      }
      parser_consume(parser, &token, TOKEN_IDENTIFIER);
      result->type = AST_STMT_TYPE_LET;
      result->let.ident = token;
      parser_consume(parser, NULL, TOKEN_ASSIGN);
      result->let.init_expr = parse_expression(parser);
      result->let.ty = ty;
      break;

    case TOKEN_KW_ITER: {
      parser_consume(parser, NULL, TOKEN_KW_ITER);
      result->type = AST_STMT_TYPE_ITER;
      result->iter.range = parse_range(parser);
      parser_consume(parser, &token, TOKEN_IDENTIFIER);
      result->iter.index.ident = token;
      parse_block(parser, &result->iter.block);
    } break;

    case TOKEN_KW_STORE: {
      parser_consume(parser, NULL, TOKEN_KW_STORE);
      result->type = AST_STMT_TYPE_STORE;
      result->store.lhs = parse_factor(parser);
      result->store.rhs = parse_expression(parser);
    } break;

    case TOKEN_KW_RETURN:
      parser_consume(parser, NULL, TOKEN_KW_RETURN);
      result->type = AST_STMT_TYPE_RETURN;
      result->expr = parse_expression(parser);
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
  return parse_expression_inner(parser, 0);
}

static struct ast_expr *parse_expression_inner(struct parser *parser, int min_prec) {
  struct ast_expr *left = parse_factor(parser);

  while (1) {
    enum token_id peek = parser_peek(parser);
    int binop = binary_op(peek);
    if (binop < 0) {
      break;
    }

    int prec = binary_op_prec(binop);
    if (prec < min_prec) {
      break;
    }

    parser_consume(parser, NULL, peek);

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

  int peek = parser_peek(parser);
  lexer_locate(parser->lexer, &result->loc);
  switch (peek) {
    // unary expression (higher precedence than binary operators)
    case TOKEN_MINUS:
    case TOKEN_TILDE:
    case TOKEN_NOT:
      parser_consume(parser, &token, peek);
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
      parser_consume(parser, &token, peek);
      result->type = AST_EXPR_TYPE_CONSTANT;
      result->constant.constant = token;

      // constants have a fully resolved type immediately
      if (peek == TOKEN_INTEGER) {
        // constants are default i64
        result->ty.ty = AST_TYPE_INTEGER;
        result->ty.integer.is_signed = 1;
        result->ty.integer.width = 64;
      } else if (peek == TOKEN_STRING) {
        result->ty.ty = AST_TYPE_STRING;
      } else if (peek == TOKEN_CHAR) {
        result->ty.ty = AST_TYPE_CHAR;
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
      struct ast_ty element_ty = parse_type(parser);
      parser_consume(parser, NULL, TOKEN_LBRACE);
      result->type = AST_EXPR_TYPE_CONSTANT;
      result->ty.ty = AST_TYPE_ARRAY;
      result->ty.array.element_ty = calloc(1, sizeof(struct ast_ty));
      *result->ty.array.element_ty = element_ty;
      result->list = parse_expression_list(parser, TOKEN_RBRACE, 0);
      result->ty.array.width = result->list->num_elements;
      parser_consume(parser, NULL, TOKEN_RBRACE);
    } break;

    // vec literals
    case TOKEN_LT: {
      parser_consume(parser, NULL, TOKEN_LT);
      result->type = AST_EXPR_TYPE_CONSTANT;
      result->ty.ty = AST_TYPE_FVEC;
      result->list = parse_expression_list(parser, TOKEN_GT, 1);
      result->ty.fvec.width = result->list->num_elements;
      parser_consume(parser, NULL, TOKEN_GT);
    } break;

      // identifiers (deref, function call, index)
    case TOKEN_IDENTIFIER:
      parser_consume(parser, &token, TOKEN_IDENTIFIER);

      if (parser_peek(parser) == TOKEN_PERIOD) {
        parser_consume(parser, NULL, TOKEN_PERIOD);

        result->type = AST_EXPR_TYPE_DEREF;
        result->deref.ident = token;
        parser_consume(parser, &token, TOKEN_IDENTIFIER);
        int field = deref_to_index(token.value.identv.ident);
        if (field < 0) {
          parser_diag(1, parser, &token, "unknown field %s in deref\n", token.value.identv.ident);
        }
        result->deref.field = field;
      } else if (parser_peek(parser) == TOKEN_LBRACKET) {
        parser_consume(parser, NULL, TOKEN_LBRACKET);
        result->type = AST_EXPR_TYPE_ARRAY_INDEX;
        result->array_index.ident = token;
        result->array_index.index = parse_expression(parser);
        parser_consume(parser, NULL, TOKEN_RBRACKET);
      } else if (parser_peek(parser) == TOKEN_LPAREN) {
        parser_consume(parser, NULL, TOKEN_LPAREN);
        result->type = AST_EXPR_TYPE_CALL;
        result->call.ident = token;
        result->call.args = parse_expression_list(parser, TOKEN_RPAREN, 0);
        parser_consume(parser, NULL, TOKEN_RPAREN);
      } else if (parser_peek(parser) == TOKEN_ASSIGN) {
        parser_consume(parser, NULL, TOKEN_ASSIGN);
        result->type = AST_EXPR_TYPE_ASSIGN;
        result->assign.ident = token;
        result->assign.expr = parse_expression(parser);
      } else {
        result->type = AST_EXPR_TYPE_VARIABLE;
        result->variable.ident = token;
      }
      break;

    // sub-expressions
    case TOKEN_LPAREN:
      free(result);
      parser_consume(parser, NULL, TOKEN_LPAREN);
      result = parse_expression(parser);
      parser_consume(parser, NULL, TOKEN_RPAREN);
      break;

      // sub-blocks
    case TOKEN_LBRACE:
      result->type = AST_EXPR_TYPE_BLOCK;
      parse_block(parser, &result->block);
      break;

      // type casts
    case TOKEN_KW_AS:
      // as <ty> <expr>
      parser_consume(parser, NULL, TOKEN_KW_AS);
      result->type = AST_EXPR_TYPE_CAST;
      result->cast.ty = parse_type(parser);
      result->cast.expr = parse_factor(parser);
      break;

      // ptr to value
    case TOKEN_KW_REF:
      // ref <expr>
      parser_consume(parser, NULL, TOKEN_KW_REF);
      result->type = AST_EXPR_TYPE_REF;
      result->ref.expr = parse_factor(parser);
      break;

      // deref ptr
    case TOKEN_KW_LOAD:
      // load <expr>
      parser_consume(parser, NULL, TOKEN_KW_LOAD);
      result->type = AST_EXPR_TYPE_LOAD;
      result->load.expr = parse_factor(parser);
      break;

    // if expressions (which evaluate to a value) or statements (no value)
    case TOKEN_KW_IF:
      parser_consume(parser, NULL, TOKEN_KW_IF);
      result->type = AST_EXPR_TYPE_IF;
      result->if_expr.cond = parse_expression(parser);
      parse_block(parser, &result->if_expr.then_block);
      if (parser_peek(parser) == TOKEN_KW_ELSE) {
        result->if_expr.has_else = 1;
        parser_consume(parser, NULL, TOKEN_KW_ELSE);
        parse_block(parser, &result->if_expr.else_block);
      } else {
        result->if_expr.has_else = 0;
      }
      break;

    // match expressions
    case TOKEN_KW_MATCH:
      parser_consume(parser, NULL, TOKEN_KW_MATCH);
      result->type = AST_EXPR_TYPE_MATCH;
      result->match.expr = parse_expression(parser);
      parser_consume(parser, NULL, TOKEN_LBRACE);
      while (parser_peek(parser) != TOKEN_RBRACE) {
        struct ast_expr_match_arm *arm = calloc(1, sizeof(struct ast_expr_match_arm));
        int is_otherwise = 0;
        if (parser_peek(parser) == TOKEN_UNDER) {
          parser_consume(parser, NULL, TOKEN_UNDER);
          arm->pattern = NULL;
          is_otherwise = 1;
        } else {
          arm->pattern = parse_expression(parser);
        }
        parser_consume(parser, NULL, TOKEN_INTO);
        arm->expr = parse_expression(parser);

        if (is_otherwise) {
          if (result->match.otherwise) {
            parser_diag(1, parser, &parser->peek, "multiple otherwise arms in match expression\n");
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
      parser_consume(parser, NULL, TOKEN_RBRACE);
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

  int peek = parser_peek(parser);
  if (peek == TOKEN_TY_SIGNED || peek == TOKEN_TY_UNSIGNED) {
    parser_consume(parser, &token, peek);

    result.ty = AST_TYPE_INTEGER;
    result.integer.is_signed = peek == TOKEN_TY_SIGNED;
    result.integer.width = token.value.tyv.dimension;
  } else if (peek == TOKEN_TY_STR) {
    parser_consume(parser, &token, TOKEN_TY_STR);

    result.ty = AST_TYPE_STRING;
  } else if (peek == TOKEN_TY_CHAR) {
    parser_consume(parser, &token, TOKEN_TY_CHAR);

    result.ty = AST_TYPE_CHAR;
  } else if (peek == TOKEN_TY_FLOAT) {
    parser_consume(parser, &token, TOKEN_TY_FLOAT);

    result.ty = AST_TYPE_FLOAT;
  } else if (peek == TOKEN_TY_FVEC) {
    parser_consume(parser, &token, TOKEN_TY_FVEC);

    result.ty = AST_TYPE_FVEC;
    result.fvec.width = token.value.tyv.dimension;
  } else if (peek == TOKEN_TY_VOID) {
    parser_consume(parser, &token, TOKEN_TY_VOID);

    result.ty = AST_TYPE_VOID;
  } else {
    parser_diag(1, parser, &parser->peek, "unexpected token of type %s when parsing type\n",
                token_id_to_string(peek));
  }

  if (parser_peek(parser) == TOKEN_ASTERISK) {
    parser_consume(parser, NULL, TOKEN_ASTERISK);
    result.flags |= TYPE_FLAG_PTR;
  }

  if (parser_peek(parser) == TOKEN_LBRACKET) {
    parser_consume(parser, NULL, TOKEN_LBRACKET);
    struct ast_ty *element_ty = calloc(1, sizeof(struct ast_ty));
    memcpy(element_ty, &result, sizeof(struct ast_ty));
    result.array.element_ty = element_ty;
    parser_consume(parser, &token, TOKEN_INTEGER);
    result.array.width = token.value.tyv.dimension;
    parser_consume(parser, NULL, TOKEN_RBRACKET);

    result.ty = AST_TYPE_ARRAY;
  }

  return result;
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

static int deref_to_index(const char *deref) {
  if (isdigit(*deref)) {
    // numeric deref
    return strtol(deref, NULL, 10);
  }

  if (deref[1] == 0) {
    // single character deref
    switch (deref[0]) {
      // XYZW
      case 'x':
      case 'y':
      case 'z':
        return deref[0] - 'x';
      case 'w':
        return 3;

      // RGBA
      case 'r':
        return 0;
      case 'g':
        return 1;
      case 'b':
        return 2;
      case 'a':
        return 3;
    }
  }

  return -1;
}

static struct ast_range parse_range(struct parser *parser) {
  struct ast_range result;

  result.start = parse_expression(parser);
  parser_consume(parser, NULL, TOKEN_COLON);
  result.end = parse_expression(parser);

  if (parser_peek(parser) == TOKEN_COLON) {
    parser_consume(parser, NULL, TOKEN_COLON);
    result.step = parse_expression(parser);
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
