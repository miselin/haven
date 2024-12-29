#include <errno.h>
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
  fprintf(stderr, "  -c             generate an object file, do not link\n");
  fprintf(stderr, "  -o <file>      output file\n");
  fprintf(stderr, "  -S             output assembly\n");
  fprintf(stderr, "  -O0            no optimizations\n");
  fprintf(stderr, "  -O1            light optimizations\n");
  fprintf(stderr, "  -O2            normal optimizations\n");
  fprintf(stderr, "  -O3            aggressive optimizations\n");
  fprintf(stderr, "  --debug-ast    display the parsed AST\n");
  fprintf(stderr, "  --debug-ir     display the generated LLVM IR before emission\n");
  fprintf(stderr, "  --debug-llvm   enable LLVM internal logging\n");
  fprintf(stderr, "  --emit-ir      emit textual IR instead of an object file\n");
  fprintf(stderr, "  --emit-bitcode emit binary IR instead of an object file\n");
  fprintf(stderr, "  --verbose      enable internal compiler logging\n");
  fprintf(stderr, "  --trace        enable even more verbose internal compiler logging\n");
  fprintf(stderr, "  -I <path>      add a path to the import search path\n");
  fprintf(stderr, "  --no-preamble  do not emit the default preamble\n");
  fprintf(stderr,
          "  --Xl <flag>    pass <flag> to the linker; use commas to merge multiple words\n");
  fprintf(stderr, "  --ld <pathh>   use <path> as the linker\n");
  fprintf(stderr, "  --no-color     disable color in diagnostics\n");
  fprintf(stderr, "  --asan         enable the Address Sanitizer\n");
}

int parse_flags(struct compiler *into, int argc, char *const argv[]) {
  if (!argc && !argv) {
    // used for testing - retain default values
    return 0;
  }

  if (!isatty(2)) {
    // into->flags[0] |= FLAG_NO_COLOR;
  }

  struct linker_option *prev_lo = NULL;

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
                                  {"debug-llvm", no_argument, 0, DebugLLVM},
                                  {"no-preamble", no_argument, 0, NoPreamble},
                                  {"Xl", required_argument, 0, LinkerOption},
                                  {"ld", required_argument, 0, Linker},
                                  {"asan", no_argument, 0, AddressSanitizer},
                                  {"trace", no_argument, 0, TraceLogs},
                                  {0, 0, 0, 0}};

  int opt;
  while ((opt = getopt_long(argc, argv, "co:SI:", long_options, &index)) != -1) {
    switch (opt) {
      case 'c':
        into->output_format = OutputObject;
        break;
      case 'S':
        into->output_format = OutputASM;
        break;
      case 'o': {
        into->output_file = copy_to_heap(optarg);
      } break;
      case 'I': {
        char *fullpath = realpath(optarg, NULL);
        if (!fullpath) {
          fprintf(stderr, "failed to resolve include path %s: %s\n", optarg, strerror(errno));
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
      case DebugLLVM:
        into->flags[0] |= FLAG_DEBUG_LLVM;
        break;
      case NoPreamble:
        into->flags[0] |= FLAG_NO_PREAMBLE;
        break;
      case AddressSanitizer:
        into->flags[0] |= FLAG_ASAN;
        break;
      case LinkerOption: {
        char *flag = strdup(optarg);
        char *saveptr;
        char *token = strtok_r(flag, ",", &saveptr);
        while (token) {
          struct linker_option *lo = (struct linker_option *)malloc(sizeof(struct linker_option));
          lo->option = copy_to_heap(token);
          lo->next = NULL;
          if (prev_lo) {
            prev_lo->next = lo;
          } else {
            into->linker_options = lo;
          }
          prev_lo = lo;

          token = strtok_r(NULL, ",", &saveptr);
        }
        free(flag);
      } break;
      case Linker: {
        into->ld = copy_to_heap(optarg);
      } break;
      case TraceLogs:
        into->flags[0] |= FLAG_TRACE;
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
      fprintf(stderr, "failed to resolve input path %s: %s\n", argv[i], strerror(errno));
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
    if (*ext) {
      if (!dot) {
        strcat((char *)into->output_file, ".");
      } else {
        *(dot + 1) = '\0';
      }

      strcat((char *)into->output_file, ext);
    } else if (dot) {
      *dot = '\0';
    }
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
