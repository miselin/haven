#include <stdlib.h>
#include <string.h>

#include "internal.h"
#include "parse.h"

/**
 * Parse a variable declaration - for use in function parameter lists
 */
struct ast_vdecl *parse_parse_vdecl(struct parser *parser) {
  uint64_t flags = 0;
  if (parser_peek(parser) == TOKEN_KW_MUT) {
    parser_consume_peeked(parser, NULL);
    flags |= DECL_FLAG_MUT;
  }

  struct ast_ty ty = parse_type(parser);
  if (type_is_error(&ty)) {
    return NULL;
  }

  struct ast_vdecl *vdecl = calloc(1, sizeof(struct ast_vdecl));
  vdecl->ty = ty;
  vdecl->flags = flags;

  struct token token;
  if (parser_consume(parser, &token, TOKEN_IDENTIFIER) < 0) {
    free(vdecl);
    return NULL;
  }
  vdecl->ident = token;

  return vdecl;
}

struct ast_toplevel *parser_parse_tydecl(struct parser *parser) {
  struct ast_toplevel *decl = calloc(1, sizeof(struct ast_toplevel));
  decl->type = AST_DECL_TYPE_TYDECL;
  lexer_locate(parser->lexer, &decl->loc);

  if (parser_consume(parser, &decl->tydecl.ident, TOKEN_IDENTIFIER) < 0) {
    free(decl);
    return NULL;
  }

  if (parser_peek(parser) != TOKEN_SEMI) {
    if (parser_consume(parser, NULL, TOKEN_ASSIGN) < 0) {
      free(decl);
      return NULL;
    }

    decl->tydecl.ty = parse_type(parser);
    strncpy(decl->tydecl.ty.name, decl->tydecl.ident.value.identv.ident, 256);
  } else {
    // forward declaration of a type that will be defined soon
    decl->tydecl.ty.ty = AST_TYPE_CUSTOM;
    decl->tydecl.ty.custom.is_forward_decl = 1;
    strncpy(decl->tydecl.ty.name, decl->tydecl.ident.value.identv.ident, 256);
  }

  if (parser_consume(parser, NULL, TOKEN_SEMI) < 0) {
    free(decl);
    return NULL;
  }
  return decl;
}

struct ast_toplevel *parser_parse_toplevel(struct parser *parser) {
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
    if (!result) {
      return NULL;
    }
    result->loc = loc;
    return result;
  } else if (peek == TOKEN_KW_TYPE) {
    parser_consume_peeked(parser, NULL);
    struct ast_toplevel *result = parser_parse_tydecl(parser);
    if (!result) {
      return NULL;
    }
    result->loc = loc;
    return result;
  } else if (peek == TOKEN_KW_IMPORT || peek == TOKEN_KW_CIMPORT) {
    parser_consume_peeked(parser, NULL);
    struct ast_toplevel *result =
        parser_parse_import(parser, peek == TOKEN_KW_IMPORT ? ImportTypeHaven : ImportTypeC);
    if (!result) {
      return NULL;
    }
    if (parser_consume(parser, NULL, TOKEN_SEMI) < 0) {
      return NULL;
    }
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
    peek = parser_peek(parser);
    while (peek != TOKEN_RPAREN && peek != TOKEN_EOF && peek != TOKEN_UNKNOWN) {
      if (peek == TOKEN_ASTERISK) {
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

      peek = parser_peek(parser);
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
      if (peek == TOKEN_KW_INTRINSIC) {
        parser_consume_peeked(parser, NULL);
        if (parser_consume(parser, &token, TOKEN_STRING) < 0) {
          free(decl);
          return NULL;
        }
        strncpy(fdecl->intrinsic, token.value.strv.s, 256);
        fdecl->is_intrinsic = 1;

        peek = parser_peek(parser);
        while (peek != TOKEN_SEMI) {
          // parse intrinsic types for overloaded intrinsics
          struct ast_ty intrinsic_ty = parse_type(parser);
          if (type_is_error(&intrinsic_ty) || type_is_tbd(&intrinsic_ty)) {
            parser_diag(1, parser, NULL,
                        "expected concrete, resolved type in intrinsic declaration");
            free(decl);
            return NULL;
          }

          if (!fdecl->intrinsic_tys) {
            fdecl->intrinsic_tys = calloc(1, sizeof(struct ast_ty));
            fdecl->intrinsic_tys[0] = intrinsic_ty;
          } else {
            size_t new_size = fdecl->num_intrinsic_tys + 1;
            fdecl->intrinsic_tys = realloc(fdecl->intrinsic_tys, new_size * sizeof(struct ast_ty));
            fdecl->intrinsic_tys[new_size - 1] = intrinsic_ty;
          }

          fdecl->num_intrinsic_tys++;

          if (parser_peek(parser) == TOKEN_COMMA) {
            parser_consume_peeked(parser, NULL);
          } else {
            break;
          }
        }
      }

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

struct ast_toplevel *parser_parse_preproc(struct parser *parser) {
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
    if (peek == TOKEN_NEWLINE || peek == TOKEN_EOF || peek == TOKEN_UNKNOWN) {
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

struct ast_toplevel *parser_parse_import(struct parser *parser, enum ImportType type) {
  struct lex_locator loc;
  lexer_locate(parser->lexer, &loc);

  struct token token;
  if (parser_consume(parser, &token, TOKEN_STRING) < 0) {
    return NULL;
  }

  if (compiler_parse_import(parser->compiler, type, token.value.strv.s) < 0) {
    return NULL;
  }

  struct ast_toplevel *result = calloc(1, sizeof(struct ast_toplevel));
  result->type = AST_DECL_TYPE_IMPORT;
  result->loc = loc;
  return result;
}
