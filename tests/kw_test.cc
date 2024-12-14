#include <gtest/gtest.h>

#include "compiler.h"
#include "lex.h"
#include "lexer/internal.h"
#include "tokens.h"
#include "trie.h"

TEST(LexerKeywords, LookupLetterInKeyword) {
  struct compiler *compiler = new_compiler(0, NULL);
  struct lex_state *state = new_lexer(stdin, "<stdin>", compiler);

  struct token token;
  token.ident = TOKEN_IDENTIFIER;
  strcpy(token.value.identv.ident, "t");
  int rc = lex_maybe_keyword_trie(state, &token);

  dump_trie(state->keywords);

  EXPECT_EQ(rc, 0);
  EXPECT_EQ(token.ident, TOKEN_IDENTIFIER);

  destroy_lexer(state);
  destroy_compiler(compiler);
}

TEST(LexerKeywords, LookupEarlyTerminal) {
  struct compiler *compiler = new_compiler(0, NULL);
  struct lex_state *state = new_lexer(stdin, "<stdin>", compiler);

  struct token token;
  token.ident = TOKEN_IDENTIFIER;
  strcpy(token.value.identv.ident, "str");
  int rc = lex_maybe_keyword_trie(state, &token);

  EXPECT_EQ(rc, 0);
  EXPECT_EQ(token.ident, TOKEN_TY_STR);

  token.ident = TOKEN_IDENTIFIER;
  strcpy(token.value.identv.ident, "struct");
  rc = lex_maybe_keyword_trie(state, &token);

  EXPECT_EQ(rc, 0);
  EXPECT_EQ(token.ident, TOKEN_KW_STRUCT);

  destroy_lexer(state);
  destroy_compiler(compiler);
}
