add_library(cimport_c STATIC cimport.c)
target_link_libraries(cimport_c PUBLIC libclang)
target_link_libraries(cimport_c PRIVATE haven_compiler_options)
