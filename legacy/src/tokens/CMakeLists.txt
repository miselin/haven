add_library(tokens OBJECT tokens.c)
target_link_libraries(tokens PRIVATE haven_compiler_options)