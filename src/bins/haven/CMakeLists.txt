
# temporary bootstrap build that doesn't include Haven code
add_executable(haven_bootstrap main.c)
target_link_libraries(haven_bootstrap haven_compiler_options compiler)
set_target_properties(haven_bootstrap PROPERTIES LINKER_LANGUAGE CXX)

# final compiler build including Haven code
add_executable(haven main.c)
target_link_libraries(haven haven_compiler_options compiler -Wl,--whole-archive cimport -Wl,--no-whole-archive)

# Use C compiler to build, but C++ compiler for linking due to LLVM
set_target_properties(haven PROPERTIES LINKER_LANGUAGE CXX)
