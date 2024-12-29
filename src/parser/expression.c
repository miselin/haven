#include <stdlib.h>
#include <string.h>

#include "internal.h"
#include "lex.h"
#include "parse.h"
#include "tokens.h"
#include "types.h"

static struct ast_expr *parse_expression_inner(struct parser *parser, int min_prec);

struct ast_expr_list *parse_expression_list(struct parser *parser, enum token_id terminator,
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

struct ast_expr *parse_expression(struct parser *parser) {
  return parse_expression_inner(parser, 0);
}

static struct ast_expr *parse_expression_inner(struct parser *parser, int min_prec) {
  struct ast_expr *left = parse_factor(parser);
  if (!left) {
    return NULL;
  }

  while (1) {
    enum token_id peek = parser_peek(parser);
    if (peek == TOKEN_UNKNOWN) {
      parser_diag(1, parser, NULL, "unexpected lexer token in expression");
    }

    // handle assignment in the middle of an expression
    if (peek == TOKEN_ASSIGN) {
      parser_consume_peeked(parser, NULL);

      compiler_log(parser->compiler, LogLevelDebug, "parser",
                   "parsing RHS of assignment in expression");
      struct ast_expr *rhs = parse_expression(parser);  // reset precedence on RHS
      if (!rhs) {
        free_expr(parser->compiler, left);
        return NULL;
      }

      compiler_log(parser->compiler, LogLevelDebug, "parser", "generating assignment as new LHS");
      struct ast_expr *new_left = calloc(1, sizeof(struct ast_expr));
      lexer_locate(parser->lexer, &new_left->loc);
      new_left->type = AST_EXPR_TYPE_ASSIGN;
      new_left->assign.lhs = left;
      new_left->assign.expr = rhs;

      left = new_left;
      continue;
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

    compiler_log(parser->compiler, LogLevelDebug, "parser", "binop is %d", binop);

    struct ast_expr *new_left = calloc(1, sizeof(struct ast_expr));
    lexer_locate(parser->lexer, &new_left->loc);
    new_left->type = AST_EXPR_TYPE_BINARY;
    new_left->binary.op = binop;
    new_left->binary.lhs = left;
    compiler_log(parser->compiler, LogLevelDebug, "parser",
                 "parsing RHS of binary expression with min_prec %d", prec + 1);
    new_left->binary.rhs = parse_expression_inner(parser, prec + 1);
    if (!new_left->binary.rhs) {
      free_expr(parser->compiler, new_left);
      return NULL;
    }

    left = new_left;
  }

  return left;
}

struct ast_expr *parse_factor(struct parser *parser) {
  struct ast_expr *result = calloc(1, sizeof(struct ast_expr));
  result->parsed_ty.ty = AST_TYPE_TBD;

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
        result->parsed_ty.ty = AST_TYPE_INTEGER;
        result->parsed_ty.flags |= TYPE_FLAG_CONSTANT;
        result->parsed_ty.integer.is_signed = 1;
        if (token.value.intv.val == 0) {
          result->parsed_ty.integer.width = 1;
        } else {
          // width is the number of bits required to represent the value
          // also, constants are all positive, so add one bit for sign
          result->parsed_ty.integer.width =
              (8 * sizeof(token.value.intv.val) - (uint64_t)__builtin_clzll(token.value.intv.val)) +
              1;
        }
      } else if (peek == TOKEN_STRING) {
        result->parsed_ty.ty = AST_TYPE_STRING;
      } else if (peek == TOKEN_CHAR) {
        result->parsed_ty.ty = AST_TYPE_INTEGER;
        result->parsed_ty.integer.is_signed = 1;
        result->parsed_ty.integer.width = 8;
      } else if (peek == TOKEN_FLOAT) {
        result->parsed_ty.ty = AST_TYPE_FLOAT;
      }

      break;

    // array literals <ty> { <expr>, <expr>, ... }
    case TOKEN_TY_CHAR:
    case TOKEN_TY_FLOAT:
    case TOKEN_TY_FVEC:
    case TOKEN_TY_SIGNED:
    case TOKEN_TY_UNSIGNED:
    case TOKEN_TY_STR:
    case TOKEN_TY_VOID:
    case TOKEN_TY_MAT: {
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

    // struct literals union <ty>::<field>(<expr>)
    case TOKEN_KW_UNION: {
      parser_consume_peeked(parser, NULL);

      result->type = AST_EXPR_TYPE_UNION_INIT;
      result->union_init.parsed_ty = parse_type(parser);
      if (parser_consume(parser, NULL, TOKEN_COLONCOLON) < 0) {
        free(result);
        return NULL;
      }

      if (parser_consume(parser, &result->union_init.field, TOKEN_IDENTIFIER) < 0) {
        free(result);
        return NULL;
      }

      if (parser_consume(parser, NULL, TOKEN_LPAREN) < 0) {
        free(result);
        return NULL;
      }
      result->union_init.inner = parse_expression(parser);
      if (!result->union_init.inner) {
        free(result);
        return NULL;
      }
      if (parser_consume(parser, NULL, TOKEN_RPAREN) < 0) {
        free(result);
        return NULL;
      }
    } break;

    // vec literals
    case TOKEN_LT: {
      parser_consume_peeked(parser, NULL);

      // < <expr>, <expr>, ... >

      result->type = AST_EXPR_TYPE_CONSTANT;
      result->parsed_ty.ty = AST_TYPE_FVEC;
      result->list = parse_expression_list(parser, TOKEN_GT, 1);
      if (!result->list) {
        free(result);
        parser_diag(1, parser, NULL, "failed to parse expression list for vector initializer");
        return NULL;
      }
      result->parsed_ty.fvec.width = result->list->num_elements;
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
      } else if (next == TOKEN_LPAREN) {
        parser_consume_peeked(parser, NULL);
        result->type = AST_EXPR_TYPE_CALL;
        result->call.ident = token;
        result->call.args = parse_expression_list(parser, TOKEN_RPAREN, 0);
        if (parser_consume(parser, NULL, TOKEN_RPAREN) < 0) {
          free(result);
          return NULL;
        }
      } else if (next == TOKEN_COLONCOLON) {
        parser_consume_peeked(parser, NULL);
        result->type = AST_EXPR_TYPE_ENUM_INIT;
        result->enum_init.enum_ty_name = token;

        if (parser_peek(parser) == TOKEN_LT) {
          parser_consume_peeked(parser, NULL);

          struct ast_template_ty *last = NULL;
          while (parser_peek(parser) != TOKEN_GT) {
            struct ast_template_ty *tmpl = calloc(1, sizeof(struct ast_template_ty));
            tmpl->parsed_ty = parse_type(parser);
            if (tmpl->parsed_ty.ty == AST_TYPE_ERROR) {
              free(tmpl);
              free(result);
              return NULL;
            }

            if (!result->enum_init.tmpls) {
              result->enum_init.tmpls = tmpl;
            } else {
              last->next = tmpl;
            }

            last = tmpl;
          }

          if (parser_consume(parser, NULL, TOKEN_GT) < 0) {
            free(result);
            return NULL;
          }

          if (parser_consume(parser, NULL, TOKEN_COLONCOLON) < 0) {
            free(result);
            return NULL;
          }
        }

        if (parser_consume(parser, &result->enum_init.enum_val_name, TOKEN_IDENTIFIER) < 0) {
          free(result);
          return NULL;
        }

        if (parser_peek(parser) == TOKEN_LPAREN) {
          result->enum_init.inner = parse_factor(parser);
          if (!result->enum_init.inner) {
            free(result);
            return NULL;
          }
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
      result->cast.parsed_ty = parse_type(parser);
      result->cast.expr = parse_factor(parser);
      break;

      // ptr to value
    case TOKEN_KW_REF:
      // ref <expr>
      parser_consume_peeked(parser, NULL);
      result->type = AST_EXPR_TYPE_REF;
      result->ref.expr = parse_factor(parser);
      break;

    // box a value
    case TOKEN_KW_BOX:
      // box <expr|type>
      parser_consume_peeked(parser, NULL);
      result->type = AST_EXPR_TYPE_BOX;

      parser->mute_diags = 1;
      parser_mark(parser);
      result->box_expr.parsed_ty.ty = AST_TYPE_TBD;
      result->box_expr.expr = parse_expression(parser);
      if (!result->box_expr.expr) {
        parser_rewind(parser);
        parser->mute_diags = 0;
        struct ast_ty ty = parse_type(parser);
        if (type_is_error(&ty)) {
          parser_diag(1, parser, &parser->peek, "expected expression or type after sizeof");
          free(result);
          return NULL;
        }

        result->box_expr.parsed_ty = ty;
      } else {
        parser_commit(parser);
      }
      parser->mute_diags = 0;
      break;

    // unbox a value
    case TOKEN_KW_UNBOX:
      // unbox <expr>
      parser_consume_peeked(parser, NULL);
      result->type = AST_EXPR_TYPE_UNBOX;
      result->box_expr.expr = parse_factor(parser);
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
      result->if_expr.has_else = 0;
      struct ast_expr_elseif *prev_elseif = NULL;
      while (parser_peek(parser) == TOKEN_KW_ELSE) {
        parser_consume_peeked(parser, NULL);
        if (parser_peek(parser) == TOKEN_KW_IF) {
          parser_consume_peeked(parser, NULL);

          struct ast_expr_elseif *elseif = calloc(1, sizeof(struct ast_expr_elseif));
          elseif->cond = parse_expression(parser);
          if (parse_block(parser, &elseif->block) < 0) {
            free(result);
            return NULL;
          }

          if (prev_elseif) {
            prev_elseif->next = elseif;
          } else {
            result->if_expr.elseifs = elseif;
          }

          prev_elseif = elseif;
        } else {
          result->if_expr.has_else = 1;
          if (parse_block(parser, &result->if_expr.else_block) < 0) {
            free(result);
            return NULL;
          }

          break;
        }
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

        parser_mark(parser);
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
            if (!arm->pattern) {
              free(result);
              return NULL;
            }
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

    case TOKEN_KW_SIZEOF: {
      parser_consume_peeked(parser, NULL);

      result->type = AST_EXPR_TYPE_SIZEOF;

      parser->mute_diags = 1;
      parser_mark(parser);
      result->sizeof_expr.parsed_ty = type_void();
      result->sizeof_expr.expr = parse_expression(parser);
      if (!result->sizeof_expr.expr) {
        parser_rewind(parser);
        parser->mute_diags = 0;
        result->sizeof_expr.parsed_ty = parse_type(parser);
        if (type_is_error(&result->sizeof_expr.parsed_ty)) {
          parser_diag(1, parser, &parser->peek, "expected expression or type after sizeof");
          free(result);
          return NULL;
        }
      } else {
        parser_commit(parser);
      }
      parser->mute_diags = 0;
    } break;

    default:
      parser_diag(1, parser, &parser->peek, "unknown token %s when parsing factor",
                  token_id_to_string(peek));
      free(result);
      return NULL;
  }

  // handle postfix operators on factors - deref, index, etc
  while (1) {
    enum token_id next = parser_peek(parser);
    if (next == TOKEN_PERIOD || next == TOKEN_DASHGT) {
      parser_consume_peeked(parser, NULL);
      compiler_log(parser->compiler, LogLevelDebug, "parser",
                   "found period in factor, wrapping with deref");

      struct ast_expr *deref = calloc(1, sizeof(struct ast_expr));
      deref->type = AST_EXPR_TYPE_DEREF;
      lexer_locate(parser->lexer, &deref->loc);
      deref->deref.is_ptr = next == TOKEN_DASHGT;
      deref->deref.target = result;
      if (parser_consume(parser, &deref->deref.field, TOKEN_IDENTIFIER) < 0) {
        free(deref);
        free(result);
        return NULL;
      }

      result = deref;
    } else if (next == TOKEN_LBRACKET) {
      parser_consume_peeked(parser, NULL);
      compiler_log(parser->compiler, LogLevelDebug, "parser",
                   "found '[' in factor, wrapping with array index");

      struct ast_expr *new_expr = calloc(1, sizeof(struct ast_expr));
      new_expr->type = AST_EXPR_TYPE_ARRAY_INDEX;
      lexer_locate(parser->lexer, &new_expr->loc);
      new_expr->array_index.target = result;
      new_expr->array_index.index = parse_expression(parser);
      if (!new_expr->array_index.index) {
        free(new_expr);
        free(result);
        return NULL;
      }

      if (parser_consume(parser, NULL, TOKEN_RBRACKET) < 0) {
        free(new_expr);
        free(result);
        return NULL;
      }

      result = new_expr;
    } else {
      break;
    }
  }

  return result;
}
