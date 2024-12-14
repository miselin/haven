
#include "compiler.h"

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cfold.h"
#include "codegen.h"
#include "lex.h"
#include "parse.h"
#include "purity.h"
#include "semantic.h"
#include "typecheck.h"
#include "utility.h"

enum Options {
  O0 = 1,
  O1,
  O2,
  O3,
  DebugAst,
  EmitIR,
  EmitBitcode,
  Verbose,
  NoColor,
  OnlyParse,
};

enum Color {
  Black,
  Red,
  Green,
  Yellow,
  Blue,
  Purple,
  Cyan,
  White,

  // not a real color
  Reset,
};

enum Attribute {
  None,
  Bold,
};

struct compiler {
  const char *input_file;
  const char *output_file;

  enum OptLevel opt_level;
  enum OutputFormat output_format;
  enum RelocationsType relocations_type;

  struct parser *parser;
  struct lex_state *lexer;

  uint64_t flags[1];

  enum Pass default_until;
};

static int parse_flags(struct compiler *into, int argc, char *const argv[]);

static const char *outext(struct compiler *compiler);

struct compiler *new_compiler(int argc, const char *argv[]) {
  struct compiler *result = calloc(1, sizeof(struct compiler));
  result->opt_level = OptNone;
  result->output_format = OutputObject;
  result->relocations_type = RelocsPIC;
  if (parse_flags(result, argc, (char *const *)argv) != 0) {
    free(result);
    return NULL;
  }
  return result;
}

enum OptLevel compiler_get_opt_level(struct compiler *compiler) {
  return compiler->opt_level;
}

enum OutputFormat compiler_get_output_format(struct compiler *compiler) {
  return compiler->output_format;
}

enum RelocationsType compiler_get_relocations_type(struct compiler *compiler) {
  return compiler->relocations_type;
}

uint64_t compiler_get_flags(struct compiler *compiler) {
  return compiler->flags[0];
}

const char *compiler_get_input_file(struct compiler *compiler) {
  return compiler->input_file;
}

const char *compiler_get_output_file(struct compiler *compiler) {
  return compiler->output_file;
}

void destroy_compiler(struct compiler *compiler) {
  free((void *)compiler->input_file);
  free((void *)compiler->output_file);
  if (compiler->parser) {
    destroy_parser(compiler->parser);
  }
  if (compiler->lexer) {
    destroy_lexer(compiler->lexer);
  }
  free(compiler);
}

static const char *copy_to_heap(const char *str) {
  size_t len = strlen(str);
  char *result = (char *)malloc(len + 1);
  strcpy(result, str);
  return result;
}

static void usage() {
  fprintf(stderr, "usage: %s [options] [file]\n", COMPILER_IDENT);
  fprintf(stderr, "  -o <file>  output file\n");
  fprintf(stderr, "  -S         output assembly\n");
  fprintf(stderr, "  -O0        no optimizations\n");
  fprintf(stderr, "  -O1        light optimizations\n");
  fprintf(stderr, "  -O2        normal optimizations\n");
  fprintf(stderr, "  -O3        aggressive optimizations\n");
  fprintf(stderr, "  --debug-ast\n");
  fprintf(stderr, "  --emit-ir\n");
  fprintf(stderr, "  --emit-bitcode\n");
  fprintf(stderr, "  --verbose\n");
}

static int parse_flags(struct compiler *into, int argc, char *const argv[]) {
  if (!argc && !argv) {
    // used for testing - retain default values
    return 0;
  }

  if (!isatty(2)) {
    into->flags[0] |= FLAG_NO_COLOR;
  }

  int index = 0;
  struct option long_options[] = {{"O0", no_argument, 0, O0},
                                  {"O1", no_argument, 0, O1},
                                  {"O2", no_argument, 0, O2},
                                  {"O3", no_argument, 0, O3},
                                  {"debug-ast", no_argument, 0, DebugAst},
                                  {"emit-ir", no_argument, 0, EmitIR},
                                  {"emit-bitcode", no_argument, 0, EmitBitcode},
                                  {"verbose", no_argument, 0, Verbose},
                                  {"no-color", no_argument, 0, NoColor},
                                  {"only-parse", no_argument, 0, OnlyParse},
                                  {0, 0, 0, 0}};

  int opt;
  while ((opt = getopt_long(argc, argv, "o:S", long_options, &index)) != -1) {
    switch (opt) {
      case 'S':
        into->output_format = OutputASM;
        break;
      case 'o': {
        into->output_file = copy_to_heap(optarg);
      } break;
      case O0:
        into->opt_level = OptNone;
        break;
      case O1:
        into->opt_level = OptLight;
        break;
      case O2:
        into->opt_level = OptNormal;
        break;
      case O3:
        into->opt_level = OptAggressive;
        break;
      case DebugAst:
        into->flags[0] |= FLAG_DISPLAY_AST;
        break;
      case EmitIR:
        into->output_format = OutputIR;
        break;
      case EmitBitcode:
        into->output_format = OutputBitcode;
        break;
      case Verbose:
        into->flags[0] |= FLAG_VERBOSE;
        break;
      case NoColor:
        into->flags[0] |= FLAG_NO_COLOR;
        break;
      case OnlyParse:
        into->default_until = PassParse;
        break;
      default:
        usage();
        return -1;
    }
  }

  for (int i = optind; i < argc; ++i) {
    if (into->input_file) {
      fprintf(stderr, "multiple input files specified\n");
      return -1;
    }

    into->input_file = copy_to_heap(argv[i]);
  }

  if (!into->input_file) {
    usage();
    return -1;
  }

  const char *ext = outext(into);

  if (!into->output_file) {
    // build the output filename from the input file
    // one extra byte for a potential dot, and one for the null terminator
    into->output_file = (const char *)malloc(strlen(into->input_file) + 1 + strlen(ext) + 1);
    strcpy((char *)into->output_file, into->input_file);
    char *dot = strrchr((char *)into->output_file, '.');
    if (!dot) {
      strcat((char *)into->output_file, ".");
    } else {
      *(dot + 1) = '\0';
    }
    strcat((char *)into->output_file, outext(into));
  }

  if (into->flags[0] & FLAG_VERBOSE) {
    fprintf(stderr, "input file: %s\n", into->input_file);
    fprintf(stderr, "output file: %s\n", into->output_file);
    fprintf(stderr, "opt level: %d\n", into->opt_level);
    fprintf(stderr, "output format: %d\n", into->output_format);
    fprintf(stderr, "relocations type: %d\n", into->relocations_type);
  }

  return 0;
}

int compiler_run(struct compiler *compiler, enum Pass until) {
  if (until == AllPasses) {
    until = compiler->default_until;
  }

  const char *filename = compiler->input_file ? compiler->input_file : "<stdin>";

  FILE *in = stdin;
  if (compiler->input_file) {
    in = fopen(compiler->input_file, "r");
    if (!in) {
      perror("fopen");
      return 1;
    }
  }

  FILE *out = stdout;
  if (compiler->output_file) {
    out = fopen(compiler->output_file, "w");
    if (!out) {
      perror("fopen");
      return 1;
    }
  }

  int rc = 0;

  struct lex_state *lexer = new_lexer(in, filename, compiler);
  struct parser *parser = new_parser(lexer, compiler);

  compiler->parser = parser;
  compiler->lexer = lexer;

  if (parser_run(parser) < 0) {
    rc = 1;
  }

  if (until == PassParse) {
    goto out;
  }

  fprintf(stderr, "result from parse: %d\n", rc);

  if (rc == 0) {
    struct cfolder *cfolder = new_cfolder(parser_get_ast(parser), compiler);
    rc = cfolder_run(cfolder);
    destroy_cfolder(cfolder);
  }

  if (until == PassCFold) {
    goto out;
  }

  fprintf(stderr, "result from cfold: %d\n", rc);

  if (rc == 0) {
    // pre-typecheck semantic pass
    struct semantic *semantic = semantic_new(parser_get_ast(parser), compiler, 0);
    rc = semantic_run(semantic);
    semantic_destroy(semantic);
  }

  if (until == PassSemantic1) {
    goto out;
  }

  fprintf(stderr, "result from first semantic pass: %d\n", rc);

  if (rc == 0) {
    struct typecheck *typecheck = new_typecheck(parser_get_ast(parser), compiler);
    rc = typecheck_run(typecheck);
    destroy_typecheck(typecheck);
  }

  if (until == PassTypecheck) {
    goto out;
  }

  fprintf(stderr, "result from typecheck pass: %d\n", rc);

  if (rc == 0) {
    struct purity *purity = purity_new(parser_get_ast(parser), compiler);
    rc = purity_run(purity);
    purity_destroy(purity);
  }

  if (until == PassPurity) {
    goto out;
  }

  fprintf(stderr, "result from purity pass: %d\n", rc);

  if (rc == 0) {
    // post-typecheck semantic pass
    struct semantic *semantic = semantic_new(parser_get_ast(parser), compiler, 1);
    rc = semantic_run(semantic);
    semantic_destroy(semantic);
  }

  if (until == PassSemantic2) {
    goto out;
  }

  fprintf(stderr, "result from second semantic pass: %d\n", rc);

  if (rc && (compiler->flags[0] & FLAG_DISPLAY_AST)) {
    fprintf(stderr, "== Partial AST after failure ==\n");
    dump_ast(parser_get_ast(parser));
  }

  if (rc == 0) {
    if (compiler->flags[0] & FLAG_DISPLAY_AST) {
      fprintf(stderr, "== Pre-codegen AST ==\n");
      dump_ast(parser_get_ast(parser));
    }

    struct codegen *codegen = new_codegen(parser_get_ast(parser), compiler);
    rc = codegen_run(codegen);
    if (rc == 0) {
      codegen_emit_ir(codegen, stderr);
      switch (compiler->output_format) {
        case OutputIR:
          rc = codegen_emit_ir(codegen, out);
          break;
        case OutputASM:
          rc = codegen_emit_asm(codegen, out);
          break;
        case OutputBitcode:
          rc = codegen_emit_bitcode(codegen, out);
          break;
        case OutputObject:
          rc = codegen_emit_obj(codegen, out);
          break;
      }
    }

    if (out != stdout) {
      fclose(out);
    }

    destroy_codegen(codegen);
  }

out:
  return rc;
}

static const char *outext(struct compiler *compiler) {
  switch (compiler->output_format) {
    case OutputIR:
      return "ll";
    case OutputASM:
      return "s";
    case OutputBitcode:
      return "bc";
    case OutputObject:
      return "o";
  }

  return "o";
}

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

struct ast_program *compiler_get_ast(struct compiler *compiler) {
  return parser_get_ast(compiler->parser);
}
