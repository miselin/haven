add_library(types STATIC types.c)
target_link_libraries(types PRIVATE haven_compiler_options)