
add_executable(haven main.c)
target_link_libraries(haven haven_compiler_options compiler)

# Use C compiler to build, but C++ compiler for linking due to LLVM
set_target_properties(haven PROPERTIES LINKER_LANGUAGE CXX)
