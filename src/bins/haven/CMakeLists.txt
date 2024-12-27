
# temporary bootstrap build that doesn't include Haven code
add_executable(haven_bootstrap main.c)
target_link_libraries(haven_bootstrap haven_compiler_options compiler_bootstrap)
set_target_properties(haven_bootstrap PROPERTIES LINKER_LANGUAGE CXX)

add_bootstrap_haven_library(haven_driver main.hv)

# final compiler build including Haven code
add_bootstrap_haven_executable(haven main.hv)
target_link_libraries(haven haven_compiler_options haven_driver compiler)

# Use C compiler to build, but C++ compiler for linking due to LLVM
set_target_properties(haven PROPERTIES LINKER_LANGUAGE CXX)
