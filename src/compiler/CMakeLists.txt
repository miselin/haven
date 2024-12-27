
add_library(compiler_c STATIC compiler.c import.c flags.c search.c diag.c)
target_link_libraries(compiler_c PRIVATE haven_compiler_options)

set (COMPILER_BASE_DEPS
    compiler_c
    ast
    lexer
    parser
    ir
    passes
    cimport_c
    types
    tokens
)

set (COMPILER_BOOTSTRAP_DEPS
    ${COMPILER_BASE_DEPS}
    utilities_bootstrap
)

set (COMPILER_DEPS
    ${COMPILER_BASE_DEPS}
    utilities
)

add_library(compiler_bootstrap INTERFACE)
target_link_libraries(compiler_bootstrap INTERFACE "$<LINK_GROUP:RESCAN,${COMPILER_BOOTSTRAP_DEPS}>")

add_library(compiler INTERFACE)
target_link_libraries(compiler INTERFACE "$<LINK_GROUP:RESCAN,${COMPILER_DEPS}>")

