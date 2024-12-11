add_library(tokens STATIC tokens.c)
target_link_libraries(tokens PRIVATE mattc_compiler_options)