
#include "compiler.h"

#include <stdlib.h>
#include <unistd.h>

#include "cfold.h"
#include "codegen.h"
#include "desugar.h"
#include "internal.h"
#include "lex.h"
#include "parse.h"
#include "purity.h"
#include "semantic.h"
#include "typecheck.h"
#include "types.h"

static int compiler_requires_linking(enum OutputFormat format);

struct compiler *new_compiler(int argc, const char *argv[]) {
  struct compiler *result = calloc(1, sizeof(struct compiler));
  result->opt_level = OptNone;
  result->output_format = OutputLinkedBinary;
  result->relocations_type = RelocsPIC;
  if (parse_flags(result, argc, (char *const *)argv) != 0) {
    free(result);
    return NULL;
  }
  result->type_repository = new_type_repository(result);
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
  struct linker_option *lo = compiler->linker_options;
  while (lo) {
    struct linker_option *next = lo->next;
    free((void *)lo->option);
    free(lo);
    lo = next;
  }
  free((void *)compiler->ld);
  struct search_dir *dir = compiler->search_dirs;
  while (dir) {
    struct search_dir *next = dir->next;
    free((void *)dir->path);
    free(dir);
    dir = next;
  }
  free((void *)compiler->input_file);
  free((void *)compiler->output_file);
  if (compiler->parser) {
    destroy_parser(compiler->parser);
  }
  if (compiler->lexer) {
    destroy_lexer(compiler->lexer);
  }
  destroy_type_repository(compiler->type_repository);
  free(compiler);
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

  const char *output_file = compiler->output_file;
  if (compiler_requires_linking(compiler->output_format)) {
    // emit to a temporary
    output_file = "tmp.haven.o";
  }

  FILE *out = stdout;
  if (compiler->output_file) {
    out = fopen(output_file, "w");
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

  if (parser_run(parser, 1) < 0) {
    rc = 1;
  }

  present_diags(compiler, parser);

  if (until == PassParse) {
    goto out;
  }

  compiler_log(compiler, LogLevelDebug, "driver", "result from parse: %d", rc);

  if (rc == 0) {
    // pre-typecheck semantic pass
    struct semantic *semantic = semantic_new(parser_get_ast(parser), compiler, 0);
    rc = semantic_run(semantic);
    semantic_destroy(semantic);
  }

  if (until == PassSemantic1) {
    goto out;
  }

  compiler_log(compiler, LogLevelDebug, "driver", "result from first semantic pass: %d", rc);

  if (rc == 0) {
    struct desugar *desugar = desugar_new(parser_get_ast(parser), compiler);
    rc = desugar_run(desugar);
    desugar_destroy(desugar);
  }

  if (until == PassDesugar) {
    goto out;
  }

  compiler_log(compiler, LogLevelDebug, "driver", "result from desugar: %d", rc);

  if (rc == 0) {
    struct cfolder *cfolder = new_cfolder(parser_get_ast(parser), compiler);
    rc = cfolder_run(cfolder);
    destroy_cfolder(cfolder);
  }

  if (until == PassCFold) {
    goto out;
  }

  compiler_log(compiler, LogLevelDebug, "driver", "result from cfold: %d", rc);

  if (rc == 0) {
    struct typecheck *typecheck = new_typecheck(parser_get_ast(parser), compiler);
    rc = typecheck_run(typecheck);
    destroy_typecheck(typecheck);
  }

  if (until == PassTypecheck) {
    goto out;
  }

  compiler_log(compiler, LogLevelDebug, "driver", "result from typecheck pass: %d", rc);

  if (rc == 0) {
    struct purity *purity = purity_new(parser_get_ast(parser), compiler);
    rc = purity_run(purity);
    purity_destroy(purity);
  }

  if (until == PassPurity) {
    goto out;
  }

  compiler_log(compiler, LogLevelDebug, "driver", "result from purity pass: %d", rc);

  if (rc == 0) {
    // post-typecheck semantic pass
    struct semantic *semantic = semantic_new(parser_get_ast(parser), compiler, 1);
    rc = semantic_run(semantic);
    semantic_destroy(semantic);
  }

  if (until == PassSemantic2) {
    goto out;
  }

  compiler_log(compiler, LogLevelDebug, "driver", "result from second semantic pass: %d", rc);

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
      if (compiler->flags[0] & FLAG_DEBUG_IR) {
        codegen_emit_ir(codegen, stderr);
      }

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
        case OutputLinkedBinary:
        case OutputLinkedLibrary:
          rc = codegen_emit_obj(codegen, out);
          break;
      }
    }

    if (out != stdout) {
      fclose(out);
    }

    destroy_codegen(codegen);
  }

  if (rc == 0 && compiler_requires_linking(compiler->output_format)) {
    // run the linker
    rc = compiler_link(compiler, output_file);
  }

out:
  if (compiler_requires_linking(compiler->output_format)) {
    unlink(output_file);
  }

  return rc;
}

const char *outext(struct compiler *compiler) {
  switch (compiler->output_format) {
    case OutputIR:
      return "ll";
    case OutputASM:
      return "s";
    case OutputBitcode:
      return "bc";
    case OutputObject:
      return "o";
    case OutputLinkedBinary:
      return "";
    case OutputLinkedLibrary:
      return "so";
  }

  return "o";
}

struct ast_program *compiler_get_ast(struct compiler *compiler) {
  return parser_get_ast(compiler->parser);
}

struct type_repository *compiler_get_type_repository(struct compiler *compiler) {
  return compiler->type_repository;
}

static int compiler_requires_linking(enum OutputFormat format) {
  return format == OutputLinkedBinary || format == OutputLinkedLibrary;
}
