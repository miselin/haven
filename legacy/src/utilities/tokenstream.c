#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lex.h"

/**
 * The token stream sits between the lexer and parser and buffers tokens.
 *
 * The parser may use this to rewind the token stream to a previous point.
 */

struct tokenentry {
  struct token token;
  struct tokenentry *next;
  struct tokenentry *prev;
};

struct tokenmarker {
  struct tokenentry *position;
  struct tokenmarker *next;
};

struct tokenstream {
  struct lex_state *lexer;
  struct tokenentry *buffer;
  struct tokenentry *tail;
  struct tokenentry *position;
  struct tokenmarker *markers;
};

struct tokenstream *new_tokenstream(struct lex_state *lexer) {
  struct tokenstream *result = calloc(1, sizeof(struct tokenstream));
  result->lexer = lexer;
  return result;
}

void destroy_tokenstream(struct tokenstream *stream) {
  while (stream->buffer) {
    struct tokenentry *entry = stream->buffer;
    stream->buffer = entry->next;
    free(entry);
  }
  free(stream);
}

int tokenstream_next_token(struct tokenstream *stream, struct token *token) {
  if (stream->position) {
    struct tokenentry *entry = stream->position;
    memcpy(token, &entry->token, sizeof(struct token));
    stream->position = entry->prev;

    return 0;
  }

  struct tokenentry *entry = calloc(1, sizeof(struct tokenentry));
  int rc = lexer_token(stream->lexer, &entry->token);
  if (rc < 0) {
    free(entry);
    return -1;
  }

  memcpy(token, &entry->token, sizeof(struct token));

  // push to buffer
  if (!stream->buffer) {
    stream->buffer = entry;
  } else {
    entry->next = stream->buffer;
    stream->buffer->prev = entry;
    stream->buffer = entry;
  }

  return 0;
}

int tokenstream_peek(struct tokenstream *stream, struct token *token) {
  if (stream->position) {
    struct tokenentry *entry = stream->position;
    memcpy(token, &entry->token, sizeof(struct token));
    return 0;
  }

  struct tokenentry *entry = calloc(1, sizeof(struct tokenentry));
  int rc = lexer_token(stream->lexer, &entry->token);
  if (rc < 0) {
    free(entry);
    return -1;
  }

  memcpy(token, &entry->token, sizeof(struct token));

  // if there's no markers, there's no point collecting a buffer as there's no rewind points
  if (!stream->markers) {
    free(entry);
    return 0;
  }

  // push to buffer
  if (!stream->buffer) {
    stream->buffer = entry;
  } else {
    entry->next = stream->buffer;
    stream->buffer->prev = entry;
    stream->buffer = entry;
  }

  return 0;
}

void tokenstream_commit(struct tokenstream *stream) {
  struct tokenentry *until = NULL;
  if (stream->markers) {
    until = stream->markers->position;
  }

  // free the buffer up until the marker
  struct tokenentry *entry = stream->buffer;
  while (entry && entry != until) {
    struct tokenentry *next = entry->next;
    free(entry);
    entry = next;
  }

  stream->buffer = until;

  // pop the last marker, we won't rewind to it anymore
  if (stream->markers) {
    struct tokenmarker *marker = stream->markers;
    stream->markers = marker->next;
    free(marker);
  }
}

void tokenstream_mark(struct tokenstream *stream) {
  if (!stream->buffer) {
    // TODO: log a warning
    return;
  }
  struct tokenmarker *marker = calloc(1, sizeof(struct tokenmarker));
  marker->position = stream->buffer;
  marker->next = stream->markers;
  stream->markers = marker;
}

void tokenstream_rewind(struct tokenstream *stream) {
  if (!stream->markers) {
    // TODO: log or something
    return;
  }
  stream->position = stream->markers->position;

  // pop the marker
  struct tokenmarker *marker = stream->markers;
  stream->markers = marker->next;
  free(marker);
}

int tokenstream_buf_empty(struct tokenstream *stream) {
  return stream->buffer == NULL;
}
