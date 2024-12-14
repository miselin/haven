add_library(tokens STATIC tokens.c)
target_link_libraries(tokens PRIVATE haven_compiler_options)