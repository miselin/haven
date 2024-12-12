#include <gtest/gtest.h>

#include "lex.h"
#include "lexer/internal.h"
#include "tokens.h"
#include "trie.h"

TEST(LexerKeywords, LookupLetterInKeyword) {
  struct lex_state *state = new_lexer(stdin, "<stdin>");

  struct token token;
  token.ident = TOKEN_IDENTIFIER;
  strcpy(token.value.identv.ident, "t");
  int rc = lex_maybe_keyword_trie(state, &token);

  EXPECT_EQ(rc, 0);
  EXPECT_EQ(token.ident, TOKEN_IDENTIFIER);

  destroy_lexer(state);
}

TEST(LexerKeywords, LookupEarlyTerminal) {
  struct lex_state *state = new_lexer(stdin, "<stdin>");

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
}
