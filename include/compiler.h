#ifndef _MATTC_COMPILER_H
#define _MATTC_COMPILER_H

#include <stdarg.h>
#include <stdint.h>

#define COMPILER_IDENT "mattc"

enum OptLevel { OptNone, OptLight, OptNormal, OptAggressive };

enum OutputFormat { OutputIR, OutputASM, OutputBitcode, OutputObject };

enum RelocationsType { RelocsPIC, RelocsStatic };

enum DiagLevel { DiagError, DiagWarning, DiagNote, DiagDebug };

enum Pass {
  AllPasses,
  PassParse,
  PassTypecheck,
  PassPurity,
  PassCFold,
  PassSemantic1,
  PassSemantic2,
};

#define FLAG_VERBOSE (1U << 0)
#define FLAG_DISPLAY_AST (1U << 1)
#define FLAG_NO_COLOR (1U << 2)

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
