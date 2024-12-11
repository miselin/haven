add_library(types STATIC types.c)
target_link_libraries(types PRIVATE mattc_compiler_options)