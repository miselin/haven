#ifndef _HAVEN_COMPILER_H
#define _HAVEN_COMPILER_H

#include <stdarg.h>
#include <stdint.h>

#define COMPILER_IDENT "haven"

enum OptLevel { OptNone, OptLight, OptNormal, OptAggressive };

enum OutputFormat { OutputIR, OutputASM, OutputBitcode, OutputObject };

enum RelocationsType { RelocsPIC, RelocsStatic };

enum DiagLevel { DiagError, DiagWarning, DiagNote, DiagDebug };

enum ImportType { ImportTypeHaven, ImportTypeC };

enum Pass {
  AllPasses,
  PassParse,
  PassTypecheck,
  PassPurity,
  PassCFold,
  PassSemantic1,
  PassSemantic2,
};

// Display verbose output including compiler internal logging during compilation
#define FLAG_VERBOSE (1U << 0)
// Display the parsed AST before emission (after all passes)
#define FLAG_DISPLAY_AST (1U << 1)
// Do not use color in diagnostics and other compiler messages
#define FLAG_NO_COLOR (1U << 2)
// Display the generated IR before emission
#define FLAG_DEBUG_IR (1U << 3)
// Turn on debugging in LLVM
#define FLAG_DEBUG_LLVM (1U << 4)

struct compiler;

#ifdef __cplusplus
extern "C" {
#endif

struct compiler *new_compiler(int argc, const char *argv[]);

enum OptLevel compiler_get_opt_level(struct compiler *compiler);
enum OutputFormat compiler_get_output_format(struct compiler *compiler);
enum RelocationsType compiler_get_relocations_type(struct compiler *compiler);

uint64_t compiler_get_flags(struct compiler *compiler);

// if null, use stdin
const char *compiler_get_input_file(struct compiler *compiler);
const char *compiler_get_output_file(struct compiler *compiler);

int compiler_run(struct compiler *compiler, enum Pass until);

int compiler_parse_import(struct compiler *compiler, enum ImportType type, const char *name);

struct ast_program *compiler_get_ast(struct compiler *compiler);

void destroy_compiler(struct compiler *compiler);

__attribute__((__format__(__printf__, 3, 0))) int compiler_vdiag(struct compiler *compiler,
                                                                 enum DiagLevel level,
                                                                 const char *fmt, va_list args);
__attribute__((format(printf, 3, 4))) int compiler_diag(struct compiler *compiler,
                                                        enum DiagLevel level, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
