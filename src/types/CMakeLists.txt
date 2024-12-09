add_library(types STATIC types.c)
target_link_libraries(types PUBLIC mattc_compiler_options)