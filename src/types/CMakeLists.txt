add_library(types OBJECT types.c repository.c)
target_link_libraries(types PRIVATE haven_compiler_options)