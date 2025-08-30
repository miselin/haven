#ifndef _HAVEN_PARSER_INTERNAL_H
#define _HAVEN_PARSER_INTERNAL_H

#include "ast.h"
#include "lex.h"
#include "parse.h"
#include "tokens.h"

struct parser {
  struct lex_state *lexer;
  struct token peek;

  struct ast_program ast;

  int errors;
  int warnings;

  struct compiler *compiler;

  struct tokenstream *stream;

  // Mute diagnostics temporarily. Used when trying multiple parse paths to avoid diagnostics from
  // the failed paths.
  int mute_diags;

  struct parser_diag *diags;
  struct parser_diag *diags_tail;
};

struct parser_diag {
  char *message;
  struct lex_locator loc;
  enum ParserDiagSeverity severity;
  struct parser_diag *next;
};

#ifdef __cplusplus
extern "C" {
#endif

// Emit a diagnostic message from the parser.
__attribute__((format(printf, 4, 5))) void parser_diag(int fatal, struct parser *parser,
                                                       struct token *token, const char *msg, ...);

// Peek the next token in the stream, skipping over newlines
enum token_id parser_peek(struct parser *parser) __attribute__((warn_unused_result));

// Peek the next token in the stream, without skipping over newlines
enum token_id parser_peek_with_nl(struct parser *parser);

// Consume a token from the lexer and emit an error diagnostic if it does not match the expected
// token provided. Returns 0 on success, -1 on failure.
__attribute__((warn_unused_result)) int parser_consume(struct parser *parser, struct token *token,
                                                       enum token_id expected);

// Consume a token from the lexer without any checks. Use this when you know the token is correct,
// for example due to a previous peek. Without a previous peek, this function is unsafe.
int parser_consume_peeked(struct parser *parser, struct token *token);

// Put a token back into the parser's stream. If a token was pending consume, this will error.
// This should be used exceedingly rarely, as it can lead to confusion.
void parser_unconsume(struct parser *parser, struct token *token);

// Parse a top-level declaration.
struct ast_toplevel *parser_parse_toplevel(struct parser *parser);

// Parse a preprocessor declaration.
struct ast_toplevel *parser_parse_preproc(struct parser *parser);

// Parse an import top-level declaration.
struct ast_toplevel *parser_parse_import(struct parser *parser, enum ImportType type);

// Parse a block.
__attribute__((warn_unused_result)) int parse_block(struct parser *parser, struct ast_block *into);

// Parse a statement.
struct ast_stmt *parse_statement(struct parser *parser, int *ended_semi);

// Parse an expression.
struct ast_expr *parse_expression(struct parser *parser);

// Parse a factor.
struct ast_expr *parse_factor(struct parser *parser);

// Parse comma-separated expressions that could end in an alternate terminator.
// For example, "<<1,2,3>,<4,5,6>>" ends in a '>>' that needs to be split and a single '>'
// reinserted to the parser to allow for the parse to succeed.
struct ast_expr_list *parse_expression_list_alt(struct parser *parser, enum token_id terminator,
                                                int factor_only, enum token_id alt_terminator);

// Parse comma-separated expressions.
struct ast_expr_list *parse_expression_list(struct parser *parser, enum token_id terminator,
                                            int factor_only);

// Wrap the given expression in cast to the given type.
struct ast_expr *wrap_cast(struct parser *parser, struct ast_expr *expr, struct ast_ty *ty);

// Parse a type at the current location in the parser.
struct ast_ty parse_type(struct parser *parser);

// Parse a range expression.
struct ast_range parse_range(struct parser *parser);

// Parse a braced initializer list.
int parse_braced_initializer(struct parser *parser, struct ast_expr *into);

// Parse a struct or union declaration.
int parser_parse_struct_decl(struct parser *parser, struct ast_ty *into);

// Parse an enum declaration.
int parser_parse_enum_decl(struct parser *parser, struct ast_ty *into);

// Parse a pattern match expression.
struct ast_expr *parser_parse_pattern_match(struct parser *parser);

// Retrieve the AST binary operation from the given lexer token.
int binary_op(enum token_id token);

// Retrieve the precedence of the given binary operation.
int binary_op_prec(int token);

// Add preamble definitions to the AST - builtin types, type aliases, and functions.
int parser_add_preamble(struct parser *parser);

// Calling tokenstream_mark without a token peeked will rewind one more token than expected
// This is by design, but confusing. Use parser_mark to ensure there's always a token peeked.
void parser_mark(struct parser *parser);

// Rewind the parser and clear out the lookahead (peek) token so the next peek uses the rewound
// location.
void parser_rewind(struct parser *parser);

// Commit the parser's backtrack buffer. Either this or parser_rewind is required to tidy up after
// a parser_mark call.
void parser_commit(struct parser *parser);

#ifdef __cplusplus
}
#endif

#endif
