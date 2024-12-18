#include <string.h>

#include "internal.h"
#include "tokens.h"
#include "trie.h"

struct lookup {
  const char *name;
  enum token_id ident;
} keywords[] = {
    {"if", TOKEN_KW_IF},
    {"else", TOKEN_KW_ELSE},
    {"let", TOKEN_KW_LET},
    {"for", TOKEN_KW_FOR},
    {"while", TOKEN_KW_WHILE},
    {"break", TOKEN_KW_BREAK},
    {"continue", TOKEN_KW_CONTINUE},
    {"match", TOKEN_KW_MATCH},
    {"as", TOKEN_KW_AS},
    {"pub", TOKEN_KW_PUB},
    {"mut", TOKEN_KW_MUT},
    {"neg", TOKEN_KW_NEG},
    {"fn", TOKEN_KW_FN},
    {"iter", TOKEN_KW_ITER},
    {"ref", TOKEN_KW_REF},
    {"store", TOKEN_KW_STORE},
    {"load", TOKEN_KW_LOAD},
    {"ret", TOKEN_KW_RETURN},
    {"struct", TOKEN_KW_STRUCT},
    {"float", TOKEN_TY_FLOAT},
    {"str", TOKEN_TY_STR},
    {"char", TOKEN_TY_CHAR},
    {"void", TOKEN_TY_VOID},
    {"type", TOKEN_KW_TYPE},
    {"nil", TOKEN_KW_NIL},
    {"defer", TOKEN_KW_DEFER},
    {"impure", TOKEN_KW_IMPURE},
    {"enum", TOKEN_KW_ENUM},
    {"import", TOKEN_KW_IMPORT},
    {"cimport", TOKEN_KW_CIMPORT},
};

int initialize_keyword_trie(struct lex_state *state) {
  state->keywords = new_trie();
  for (size_t i = 0; i < sizeof(keywords) / sizeof(struct lookup); i++) {
    trie_insert(state->keywords, keywords[i].name, &keywords[i]);
  }

  return 0;
}

int destroy_keyword_trie(struct lex_state *state) {
  destroy_trie(state->keywords);
  return 0;
}

// public for benchmark
int lex_maybe_keyword_inner(struct lex_state *state, struct token *token, const char *ident) {
  for (size_t i = 0; i < sizeof(keywords) / sizeof(struct lookup); i++) {
    if (!strcmp(ident, keywords[i].name)) {
      token->ident = keywords[i].ident;
      return 0;
    }
  }

  if (!strncmp(ident, "fvec", 4)) {
    return lex_vector_type(state, token);
  }

  return 0;
}

// public for benchmark
int lex_maybe_keyword_trie_inner(struct lex_state *state, struct token *token, const char *ident) {
  struct lookup *entry = trie_lookup(state->keywords, token->value.identv.ident);
  if (entry) {
    token->ident = entry->ident;
    return 0;
  }

  if (!strncmp(ident, "fvec", 4)) {
    return lex_vector_type(state, token);
  }

  return 0;
}

int lex_maybe_keyword_trie(struct lex_state *state, struct token *token) {
  return lex_maybe_keyword_trie_inner(state, token, token->value.identv.ident);
}

int lex_maybe_keyword(struct lex_state *state, struct token *token) {
  return lex_maybe_keyword_inner(state, token, token->value.identv.ident);
}
