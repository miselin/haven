
add_executable(mattfmt mattfmt.c)
target_link_libraries(mattfmt mattc_compiler_options compiler)

# Use C compiler to build, but C++ compiler for linking due to LLVM
set_target_properties(mattfmt PROPERTIES LINKER_LANGUAGE CXX)
