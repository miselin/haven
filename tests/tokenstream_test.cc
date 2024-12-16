#include "tokenstream.h"

#include <gtest/gtest.h>

#include "compiler.h"
#include "lex.h"
#include "lexer/internal.h"
#include "tokens.h"
#include "trie.h"

static void push_lexer(struct lex_state *state, const char *input_str) {
  size_t len = strlen(input_str);
  for (size_t i = 0; i < len; i++) {
    lex_unget(state, input_str[i]);
  }
}

TEST(TokenStream, NoRewinding) {
  struct compiler *compiler = new_compiler(0, NULL);
  struct lex_state *state = new_lexer(stdin, "<stdin>", compiler);

  struct tokenstream *stream = new_tokenstream(state);

  push_lexer(state, "let var = 5;");

  struct token token;

  enum token_id expected[] = {TOKEN_KW_LET, TOKEN_IDENTIFIER, TOKEN_ASSIGN, TOKEN_INTEGER,
                              TOKEN_SEMI};

  for (size_t i = 0; i < sizeof(expected) / sizeof(enum token_id); i++) {
    int rc = tokenstream_next_token(stream, &token);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(token.ident, expected[i]);
  }

  tokenstream_commit(stream);

  destroy_tokenstream(stream);
  destroy_lexer(state);
  destroy_compiler(compiler);
}

TEST(TokenStream, Rewind) {
  struct compiler *compiler = new_compiler(0, NULL);
  struct lex_state *state = new_lexer(stdin, "<stdin>", compiler);

  struct tokenstream *stream = new_tokenstream(state);

  push_lexer(state, "let var = 5;");

  struct token token;

  enum token_id expected[] = {TOKEN_KW_LET, TOKEN_IDENTIFIER, TOKEN_ASSIGN, TOKEN_INTEGER,
                              TOKEN_SEMI};

  for (size_t i = 0; i < sizeof(expected) / sizeof(enum token_id); i++) {
    int rc = tokenstream_next_token(stream, &token);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(token.ident, expected[i]);

    // we will rewind the integer expression
    if (i == 3) {
      tokenstream_mark(stream);
    }
  }

  // play back the integer expression (will replay "5;")
  tokenstream_rewind(stream);

  for (size_t i = 3; i < sizeof(expected) / sizeof(enum token_id); i++) {
    int rc = tokenstream_next_token(stream, &token);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(token.ident, expected[i]);
  }

  tokenstream_commit(stream);

  destroy_tokenstream(stream);
  destroy_lexer(state);
  destroy_compiler(compiler);
}

TEST(TokenStream, MarkNoRewind) {
  struct compiler *compiler = new_compiler(0, NULL);
  struct lex_state *state = new_lexer(stdin, "<stdin>", compiler);

  struct tokenstream *stream = new_tokenstream(state);

  push_lexer(state, "let var = 5;");

  struct token token;

  enum token_id expected[] = {TOKEN_KW_LET, TOKEN_IDENTIFIER, TOKEN_ASSIGN, TOKEN_INTEGER,
                              TOKEN_SEMI};

  for (size_t i = 0; i < sizeof(expected) / sizeof(enum token_id); i++) {
    int rc = tokenstream_next_token(stream, &token);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(token.ident, expected[i]);

    if (i == 3) {
      tokenstream_mark(stream);
    }
  }

  // don't rewind, just commit
  tokenstream_commit(stream);

  // commit clears up to the last marker, if any
  EXPECT_EQ(tokenstream_buf_empty(stream), 0);

  // commit from before the marker - this should now clear the buffer
  tokenstream_commit(stream);

  // buffer should be empty now
  EXPECT_EQ(tokenstream_buf_empty(stream), 1);

  destroy_tokenstream(stream);
  destroy_lexer(state);
  destroy_compiler(compiler);
}

TEST(TokenStream, EmptyMark) {
  struct compiler *compiler = new_compiler(0, NULL);
  struct lex_state *state = new_lexer(stdin, "<stdin>", compiler);

  struct tokenstream *stream = new_tokenstream(state);

  tokenstream_mark(stream);
  tokenstream_commit(stream);
  EXPECT_EQ(tokenstream_buf_empty(stream), 1);

  destroy_tokenstream(stream);
  destroy_lexer(state);
  destroy_compiler(compiler);
}

TEST(TokenStream, EarlyMark) {
  struct compiler *compiler = new_compiler(0, NULL);
  struct lex_state *state = new_lexer(stdin, "<stdin>", compiler);
  push_lexer(state, "let var = 5;");

  struct tokenstream *stream = new_tokenstream(state);

  struct token token;

  tokenstream_mark(stream);
  tokenstream_next_token(stream, &token);
  tokenstream_rewind(stream);
  tokenstream_commit(stream);
  EXPECT_EQ(tokenstream_buf_empty(stream), 1);

  destroy_tokenstream(stream);
  destroy_lexer(state);
  destroy_compiler(compiler);
}
