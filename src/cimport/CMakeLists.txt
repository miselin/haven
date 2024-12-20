add_bootstrap_haven_library(cimport cimport.hv clex.hv)

add_library(cimport_c STATIC cimport.c)
target_link_libraries(cimport_c PUBLIC libclang ast parser)
target_link_libraries(cimport_c PRIVATE haven_compiler_options)

target_link_libraries(cimport PUBLIC cimport_c)
