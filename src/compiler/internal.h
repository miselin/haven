#ifndef _HAVEN_COMPILER_INTERNAL_H
#define _HAVEN_COMPILER_INTERNAL_H

#include <stdio.h>
#include <sys/stat.h>

#include "compiler.h"
#include "types.h"

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
  DebugIR,
  DebugLLVM,
  NoPreamble,
  LinkerOption,
  Linker,
  OnlyCompile,
  AddressSanitizer,
  TraceLogs,
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

struct search_dir {
  // TODO: type - haven or C?
  const char *path;
  struct search_dir *next;
};

struct linker_option {
  const char *option;
  struct linker_option *next;
};

struct imported_file {
  struct stat st;
  struct imported_file *next;
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

  struct search_dir *search_dirs;
  struct linker_option *linker_options;

  struct type_repository *type_repository;

  const char *ld;

  struct imported_file *imported_files;

  struct cimport *cimporter;
};

int parse_flags(struct compiler *into, int argc, char *const argv[]);

void present_diags(struct compiler *compiler, struct parser *parser);

const char *outext(struct compiler *compiler);

FILE *find_file(struct compiler *compiler, const char *filename);
int find_file_path(struct compiler *compiler, const char *filename, const char **discovered_path);

void add_search_dir(struct compiler *compiler, const char *path);

int compiler_link(struct compiler *compiler, const char *object_file);

int same_file(struct stat *a, struct stat *b);

void track_imported_file(struct compiler *compiler, struct stat *st);

#endif
