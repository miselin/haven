#include <getopt.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "compiler.h"
#include "internal.h"

static void usage(void);

static const char *copy_to_heap(const char *str) {
  size_t len = strlen(str);
  char *result = (char *)malloc(len + 1);
  strcpy(result, str);
  return result;
}

static void usage(void) {
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
  fprintf(stderr, "  -I <path>  add a path to the import search path\n");
}

int parse_flags(struct compiler *into, int argc, char *const argv[]) {
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
                                  {"debug-ir", no_argument, 0, DebugIR},
                                  {0, 0, 0, 0}};

  int opt;
  while ((opt = getopt_long(argc, argv, "o:SI:", long_options, &index)) != -1) {
    switch (opt) {
      case 'S':
        into->output_format = OutputASM;
        break;
      case 'o': {
        into->output_file = copy_to_heap(optarg);
      } break;
      case 'I': {
        char *fullpath = realpath(optarg, NULL);
        if (!fullpath) {
          perror("realpath");
          return -1;
        }

        struct stat st;
        stat(fullpath, &st);
        if (!S_ISDIR(st.st_mode)) {
          fprintf(stderr, "not a directory: %s\n", fullpath);
          return -1;
        }

        add_search_dir(into, fullpath);
        free(fullpath);
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
      case DebugIR:
        into->flags[0] |= FLAG_DEBUG_IR;
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

    // add the directory of the input file to the search path
    char *input_file_abs = realpath(argv[i], NULL);
    if (!input_file_abs) {
      perror("realpath");
      return -1;
    }

    into->input_file = strdup(input_file_abs);

    char *input_file_dir = dirname(input_file_abs);
    add_search_dir(into, input_file_dir);

    free(input_file_abs);
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
