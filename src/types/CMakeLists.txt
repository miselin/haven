add_library(types OBJECT types.c)
target_link_libraries(types PRIVATE haven_compiler_options)