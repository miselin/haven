add_library(cimport_c OBJECT cimport.c)
target_link_libraries(cimport_c PRIVATE haven_compiler_options)
