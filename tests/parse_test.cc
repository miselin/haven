#include "parse.h"

#include <gtest/gtest.h>

#include "compiler.h"
#include "lex.h"
#include "lexer/internal.h"
#include "tokens.h"
#include "tokenstream.h"
#include "trie.h"

static void push_lexer(struct lex_state *state, const char *input_str) {
  size_t len = strlen(input_str);
  for (size_t i = 0; i < len; i++) {
    lex_unget(state, input_str[i]);
  }

  // unget also unwinds line/column, which started at zero
  struct lex_locator loc;
  lexer_locate(state, &loc);
  loc.line += len;
  lexer_update_loc(state, &loc);
}

TEST(Parser, UnexpectedEOF) {
  struct compiler *compiler = new_compiler(0, NULL);
  struct lex_state *state = new_lexer(NULL, "<stdin>", compiler);
  struct parser *parser = new_parser(state, compiler);

  push_lexer(state, "type ");

  EXPECT_EQ(parser_run(parser, 0), -1);

  struct parser_diag *diag = parser_pop_diag(parser);
  EXPECT_NE(diag, nullptr);

  EXPECT_STREQ(parser_diag_msg(diag), "unexpected EOF or other error in token stream");
  EXPECT_EQ(parser_diag_severity(diag), Error);
  struct lex_locator *loc = parser_diag_loc(diag);

  EXPECT_EQ((int32_t)loc->line, 0);
  EXPECT_EQ(loc->column, 5);

  parser_free_diag(diag);

  destroy_parser(parser);
  destroy_lexer(state);
  destroy_compiler(compiler);
}

TEST(Parser, TyDeclMissingIdentifier) {
  struct compiler *compiler = new_compiler(0, NULL);
  struct lex_state *state = new_lexer(NULL, "<stdin>", compiler);
  struct parser *parser = new_parser(state, compiler);

  push_lexer(state, "type = struct { i32 x; };");

  EXPECT_EQ(parser_run(parser, 0), -1);

  struct parser_diag *diag = parser_pop_diag(parser);
  EXPECT_NE(diag, nullptr);

  EXPECT_STREQ(parser_diag_msg(diag), "unexpected token =, wanted <identifier>");
  EXPECT_EQ(parser_diag_severity(diag), Error);
  struct lex_locator *loc = parser_diag_loc(diag);

  EXPECT_EQ((int32_t)loc->line, 0);
  EXPECT_EQ(loc->column, 5);

  parser_free_diag(diag);

  destroy_parser(parser);
  destroy_lexer(state);
  destroy_compiler(compiler);
}

TEST(Parser, TyDeclMissingAssign) {
  struct compiler *compiler = new_compiler(0, NULL);
  struct lex_state *state = new_lexer(NULL, "<stdin>", compiler);
  struct parser *parser = new_parser(state, compiler);

  push_lexer(state, "type mytype struct { i32 x; };");

  EXPECT_EQ(parser_run(parser, 0), -1);

  struct parser_diag *diag = parser_pop_diag(parser);
  EXPECT_NE(diag, nullptr);

  EXPECT_STREQ(parser_diag_msg(diag), "unexpected token struct, wanted =");
  EXPECT_EQ(parser_diag_severity(diag), Error);
  struct lex_locator *loc = parser_diag_loc(diag);

  EXPECT_EQ((int32_t)loc->line, 0);
  EXPECT_EQ(loc->column, 11);

  parser_free_diag(diag);

  destroy_parser(parser);
  destroy_lexer(state);
  destroy_compiler(compiler);
}

TEST(Parser, TyDeclMissingSemi) {
  struct compiler *compiler = new_compiler(0, NULL);
  struct lex_state *state = new_lexer(NULL, "<stdin>", compiler);
  struct parser *parser = new_parser(state, compiler);

  push_lexer(state, "type mytype = i32");

  EXPECT_EQ(parser_run(parser, 0), -1);

  struct parser_diag *diag = parser_pop_diag(parser);
  EXPECT_NE(diag, nullptr);

  EXPECT_STREQ(parser_diag_msg(diag), "unexpected EOF or other error in token stream");
  EXPECT_EQ(parser_diag_severity(diag), Error);
  struct lex_locator *loc = parser_diag_loc(diag);

  EXPECT_EQ((int32_t)loc->line, 0);
  EXPECT_EQ(loc->column, 17);

  parser_free_diag(diag);

  destroy_parser(parser);
  destroy_lexer(state);
  destroy_compiler(compiler);
}
