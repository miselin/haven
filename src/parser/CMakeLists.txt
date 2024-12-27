add_library(parser OBJECT parse.c)
target_link_libraries(parser PRIVATE haven_compiler_options)