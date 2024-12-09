add_library(tokens STATIC tokens.c)
target_link_libraries(tokens PUBLIC mattc_compiler_options)