#ifndef _HAVEN_TOKENSTREAM_H
#define _HAVEN_TOKENSTREAM_H

#include "lex.h"

struct tokenstream;
struct tokenmarker;

#ifdef __cplusplus
extern "C" {
#endif

struct tokenstream *new_tokenstream(struct lex_state *lexer);
void destroy_tokenstream(struct tokenstream *stream);

int tokenstream_next_token(struct tokenstream *stream, struct token *token);

// Commit tokens in the buffer since the last mark, or the entire buffer if no marks are set.
// This frees up the tokens in the buffer. If no further rewinding will take place, you should
// call this function to free up memory.
void tokenstream_commit(struct tokenstream *stream);

// Push a new marker with the current buffer position to the marker stack.
void tokenstream_mark(struct tokenstream *stream);

// Rewind the buffer to the most recent marker, and pop the marker from the stack.
// Future calls to tokenstream_next_token will return tokens from the buffer starting at the
// position of the marker.
void tokenstream_rewind(struct tokenstream *stream);

int tokenstream_buf_empty(struct tokenstream *stream);

#ifdef __cplusplus
}
#endif

#endif
