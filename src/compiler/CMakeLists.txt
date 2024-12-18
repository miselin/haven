
add_library(compiler STATIC compiler.c import.c flags.c search.c diag.c)
target_link_libraries(compiler PUBLIC lexer parser ir passes)
target_link_libraries(compiler PRIVATE haven_compiler_options)
