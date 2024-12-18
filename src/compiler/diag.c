#include <stdio.h>

#include "compiler.h"
#include "internal.h"
#include "parse.h"

static void color(struct compiler *compiler, enum Color color, enum Attribute attr) {
  if (compiler->flags[0] & FLAG_NO_COLOR) {
    return;
  }

  if (color == Reset) {
    fprintf(stderr, "\033[0m");
    return;
  }

  fprintf(stderr, "\033[%d;%dm", attr, color + 30);
}

int compiler_vdiag(struct compiler *compiler, enum DiagLevel level, const char *fmt, va_list args) {
  if (level == DiagDebug && !(compiler->flags[0] & FLAG_VERBOSE)) {
    return 0;
  }

  enum Color level_color = White;

  const char *prefix = "";
  switch (level) {
    case DiagError:
      prefix = "error";
      level_color = Red;
      break;
    case DiagWarning:
      prefix = "warning";
      level_color = Yellow;
      break;
    case DiagNote:
      prefix = "note";
      level_color = Purple;
      break;
    case DiagDebug:
      prefix = "debug";
      break;
  }

  color(compiler, level_color, Bold);
  fprintf(stderr, "%s", prefix);
  color(compiler, Reset, None);

  fprintf(stderr, ": ");
  vfprintf(stderr, fmt, args);

  return 0;
}

int compiler_diag(struct compiler *compiler, enum DiagLevel level, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int rc = compiler_vdiag(compiler, level, fmt, args);
  va_end(args);
  return rc;
}

void present_diags(struct compiler *compiler, struct parser *parser) {
  struct parser_diag *diag = parser_pop_diag(parser);
  while (diag) {
    enum ParserDiagSeverity severity = parser_diag_severity(diag);
    enum DiagLevel level = DiagError;
    if (severity == Warning) {
      level = DiagWarning;
    }
    struct lex_locator *loc = parser_diag_loc(diag);
    compiler_diag(compiler, level, "%s:%zd:%zd: %s\n", loc->file, loc->line + 1, loc->column,
                  parser_diag_msg(diag));
    parser_free_diag(diag);
    diag = parser_pop_diag(parser);
  }
}
