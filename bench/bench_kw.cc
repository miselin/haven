#include <benchmark/benchmark.h>
#include <stdio.h>

#include "compiler.h"
#include "lex.h"

extern "C" int lex_maybe_keyword_inner(struct lex_state *state, struct token *token,
                                       const char *ident);
extern "C" int lex_maybe_keyword_trie_inner(struct lex_state *state, struct token *token,
                                            const char *ident);

struct RAIILexer {
  struct compiler *compiler;
  struct lex_state *state;
  struct token token;

  RAIILexer() {
    compiler = new_compiler(0, NULL);
    state = new_lexer(stdin, "<stdin>", compiler);
  }

  ~RAIILexer() {
    destroy_lexer(state);
    destroy_compiler(compiler);
  }
};

static void BM_KWNotFound_Trie(benchmark::State &state) {
  RAIILexer lexer;
  for (auto _ : state) {
    benchmark::DoNotOptimize(lex_maybe_keyword_trie_inner(lexer.state, &lexer.token, "foo"));
  }
}

static void BM_KWFound_Trie(benchmark::State &state) {
  RAIILexer lexer;
  for (auto _ : state) {
    benchmark::DoNotOptimize(lex_maybe_keyword_trie_inner(lexer.state, &lexer.token, "continue"));
  }
}

static void BM_KWNotFound_Array(benchmark::State &state) {
  RAIILexer lexer;
  for (auto _ : state) {
    benchmark::DoNotOptimize(lex_maybe_keyword_inner(lexer.state, &lexer.token, "foo"));
  }
}

static void BM_KWFound_Array(benchmark::State &state) {
  RAIILexer lexer;
  for (auto _ : state) {
    benchmark::DoNotOptimize(lex_maybe_keyword_inner(lexer.state, &lexer.token, "continue"));
  }
}

BENCHMARK(BM_KWNotFound_Trie);
BENCHMARK(BM_KWFound_Trie);
BENCHMARK(BM_KWNotFound_Array);
BENCHMARK(BM_KWFound_Array);

BENCHMARK_MAIN();
