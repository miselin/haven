add_library(parser STATIC parse.c)
target_link_libraries(parser PUBLIC mattc_compiler_options lexer ast tokens)