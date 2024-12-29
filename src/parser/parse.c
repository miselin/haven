#include "parse.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "compiler.h"
#include "internal.h"
#include "lex.h"
#include "tokenstream.h"

struct parser *new_parser(struct lex_state *lexer, struct compiler *compiler) {
  struct parser *result = calloc(1, sizeof(struct parser));
  result->lexer = lexer;
  result->compiler = compiler;
  result->stream = new_tokenstream(lexer);
  return result;
}

int parser_run(struct parser *parser, int root_tu) {
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

    if (!last) {
      parser->ast.decls = decl;
    } else {
      last->next = decl;
    }

    last = decl;
  }

  return root_tu ? parser_add_preamble(parser) : 0;
}

struct ast_program *parser_get_ast(struct parser *parser) {
  return &parser->ast;
}

void destroy_parser(struct parser *parser) {
  free_ast(parser->compiler, &parser->ast);
  destroy_tokenstream(parser->stream);
  free(parser);
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

int parser_merge_asts(struct parser *parser, struct parser *other) {
  if (!parser || !other) {
    return -1;
  }

  return parser_merge_program(parser, &other->ast);
}

int parser_merge_program(struct parser *parser, struct ast_program *program) {
  // insert the other AST at the end of the current AST
  struct ast_toplevel *last = parser->ast.decls;
  while (last && last->next) {
    last = last->next;
  }

  if (last) {
    last->next = program->decls;
  } else {
    parser->ast.decls = program->decls;
  }

  // clear the other parser's AST
  program->decls = NULL;

  return 0;
}

int parser_merge_into(struct parser *parser, struct ast_import *into) {
  if (into->ast) {
    compiler_log(parser->compiler, LogLevelError, "parser", "import node already has an AST");
    return -1;
  }

  into->ast = calloc(1, sizeof(struct ast_program));
  *into->ast = parser->ast;

  parser->ast.decls = NULL;

  return 0;
}
