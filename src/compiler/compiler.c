
#include "compiler.h"

#include <stdlib.h>

#include "cfold.h"
#include "codegen.h"
#include "internal.h"
#include "lex.h"
#include "parse.h"
#include "purity.h"
#include "semantic.h"
#include "typecheck.h"
#include "utility.h"

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

  if (parser_run(parser, 1) < 0) {
    rc = 1;
  }

  present_diags(compiler, parser);

  if (until == PassParse) {
    goto out;
  }

  // fprintf(stderr, "result from parse: %d\n", rc);

  if (rc == 0) {
    struct cfolder *cfolder = new_cfolder(parser_get_ast(parser), compiler);
    rc = cfolder_run(cfolder);
    destroy_cfolder(cfolder);
  }

  if (until == PassCFold) {
    goto out;
  }

  // fprintf(stderr, "result from cfold: %d\n", rc);

  if (rc == 0) {
    // pre-typecheck semantic pass
    struct semantic *semantic = semantic_new(parser_get_ast(parser), compiler, 0);
    rc = semantic_run(semantic);
    semantic_destroy(semantic);
  }

  if (until == PassSemantic1) {
    goto out;
  }

  // fprintf(stderr, "result from first semantic pass: %d\n", rc);

  if (rc == 0) {
    struct typecheck *typecheck = new_typecheck(parser_get_ast(parser), compiler);
    rc = typecheck_run(typecheck);
    destroy_typecheck(typecheck);
  }

  if (until == PassTypecheck) {
    goto out;
  }

  // fprintf(stderr, "result from typecheck pass: %d\n", rc);

  if (rc == 0) {
    struct purity *purity = purity_new(parser_get_ast(parser), compiler);
    rc = purity_run(purity);
    purity_destroy(purity);
  }

  if (until == PassPurity) {
    goto out;
  }

  // fprintf(stderr, "result from purity pass: %d\n", rc);

  if (rc == 0) {
    // post-typecheck semantic pass
    struct semantic *semantic = semantic_new(parser_get_ast(parser), compiler, 1);
    rc = semantic_run(semantic);
    semantic_destroy(semantic);
  }

  if (until == PassSemantic2) {
    goto out;
  }

  // fprintf(stderr, "result from second semantic pass: %d\n", rc);

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
  }

  return "o";
}

struct ast_program *compiler_get_ast(struct compiler *compiler) {
  return parser_get_ast(compiler->parser);
}
