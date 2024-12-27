add_library(parser STATIC parse.c)
target_link_libraries(parser PRIVATE haven_compiler_options)