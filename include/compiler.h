#ifndef _HAVEN_COMPILER_H
#define _HAVEN_COMPILER_H

#include <stdarg.h>
#include <stdint.h>

#include "types.h"

#define COMPILER_IDENT "haven"

struct ast_import;

enum OptLevel { OptNone, OptLight, OptNormal, OptAggressive, OptSize };

enum OutputFormat {
  OutputIR,
  OutputASM,
  OutputBitcode,
  OutputObject,
  OutputLinkedBinary,
  OutputLinkedLibrary
};

enum RelocationsType { RelocsPIC, RelocsStatic };

enum LogLevel { LogLevelError, LogLevelWarning, LogLevelInfo, LogLevelDebug, LogLevelTrace };

enum DiagLevel { DiagError, DiagWarning, DiagNote, DiagDebug };

enum ImportType { ImportTypeHaven, ImportTypeC };

enum Pass {
  AllPasses,
  PassParse,
  PassTypecheck,
  PassPurity,
  PassCFold,
  PassDesugar,
  PassSemantic1,
  PassSemantic2,
};

// Display verbose output including compiler internal logging during compilation
#define FLAG_VERBOSE (1UL << 0)
// Display the parsed AST before emission (after all passes)
#define FLAG_DISPLAY_AST (1UL << 1)
// Do not use color in diagnostics and other compiler messages
#define FLAG_NO_COLOR (1UL << 2)
// Display the generated IR before emission
#define FLAG_DEBUG_IR (1UL << 3)
// Turn on debugging in LLVM
#define FLAG_DEBUG_LLVM (1UL << 4)
// Don't emit a preamble
#define FLAG_NO_PREAMBLE (1UL << 5)
// Enable the Address Sanitizer
#define FLAG_ASAN (1UL << 6)
// Enable trace level debugging, which is immensely verbose.
#define FLAG_TRACE (1UL << 7)
// Disable emission of frame pointers
#define FLAG_NO_FRAME_POINTER (1UL << 8)
// Bootstrap mode, which enables cimport for bootstrapping the compiler to build itself
#define FLAG_BOOTSTRAP (1UL << 9)

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

int compiler_parse_import(struct compiler *compiler, enum ImportType type, const char *name,
                          struct ast_import *into);

int compiler_finalize_imports(struct compiler *compiler, struct ast_import *into);

struct ast_program *compiler_get_ast(struct compiler *compiler);
struct type_repository *compiler_get_type_repository(struct compiler *compiler);

// Serialize the AST into the given buffer. This can be used to save the AST for later use,
// or to pass the AST to Haven code to be transformed into Haven structures for passes.
int compiler_serialize_ast(struct compiler *compiler, char *buffer, size_t *len);

// Deserialize the given buffer containing a serialized AST and codegen it using the
// compiler object's flags (e.g. output file).  It's expected that the deserialized
// AST is in a form that is fully ready for codegen, i.e. all passes have been run,
// all types are resolved, and so on. The compiler is likely to crash if this is not
// the case.
int compiler_deserialize_and_codegen(struct compiler *compiler, char *buffer, size_t len);

// Add the given library to the link command line. It will be prefixed as needed for
// the target platform, e.g. "c" -> "-lc"
void compiler_add_link_library(struct compiler *compiler, const char *lib);

// Convert compiler flags into a form useful for cimport
// Caller must free entries and the result pointer here.
const char *const *compiler_get_cimport_compiler_flags(struct compiler *compiler, size_t *count);

void destroy_compiler(struct compiler *compiler);

__attribute__((__format__(__printf__, 3, 0))) int compiler_vdiag(struct compiler *compiler,
                                                                 enum DiagLevel level,
                                                                 const char *fmt, va_list args);
__attribute__((format(printf, 3, 4))) int compiler_diag(struct compiler *compiler,
                                                        enum DiagLevel level, const char *fmt, ...);

__attribute__((__format__(__printf__, 4, 5))) int compiler_log(struct compiler *compiler,
                                                               enum LogLevel level,
                                                               const char *subsys, const char *fmt,
                                                               ...);

#ifdef __cplusplus
}
#endif

#endif
