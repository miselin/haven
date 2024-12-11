add_library(parser STATIC parse.c)
target_link_libraries(parser PUBLIC lexer ast tokens)
target_link_libraries(parser PRIVATE mattc_compiler_options)