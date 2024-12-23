#include "lex.h"

#include <gtest/gtest.h>

#include "compiler.h"
#include "lexer/internal.h"
#include "tokens.h"
#include "tokenstream.h"
#include "trie.h"

static void push_lexer(struct lex_state *state, const char *input_str) {
  size_t len = strlen(input_str);
  for (size_t i = 0; i < len; i++) {
    lex_unget(state, input_str[i]);
  }
}

TEST(Lexer, FVecIdentifier) {
  struct compiler *compiler = new_compiler(0, NULL);
  struct lex_state *state = new_lexer(stdin, "<stdin>", compiler);

  struct tokenstream *stream = new_tokenstream(state);

  push_lexer(state, "fvec_identifier fvec4_identifier fvec4");

  struct token token;

  enum token_id expected[] = {TOKEN_IDENTIFIER, TOKEN_IDENTIFIER, TOKEN_TY_FVEC};

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

TEST(Lexer, MatIdentifier) {
  struct compiler *compiler = new_compiler(0, NULL);
  struct lex_state *state = new_lexer(stdin, "<stdin>", compiler);

  struct tokenstream *stream = new_tokenstream(state);

  push_lexer(state, "mat_identifier mat3x3_identifier mat3identx4 mat3x3");

  struct token token;

  enum token_id expected[] = {TOKEN_IDENTIFIER, TOKEN_IDENTIFIER, TOKEN_IDENTIFIER, TOKEN_TY_MAT};

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
