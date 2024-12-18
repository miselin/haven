add_library(parser STATIC parse.c)
target_link_libraries(parser PUBLIC lexer ast tokens compiler)
target_link_libraries(parser PRIVATE haven_compiler_options)