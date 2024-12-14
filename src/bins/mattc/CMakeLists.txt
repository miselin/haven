
add_executable(mattc main.c)
target_link_libraries(mattc mattc_compiler_options compiler)

# Use C compiler to build, but C++ compiler for linking due to LLVM
set_target_properties(mattc PROPERTIES LINKER_LANGUAGE CXX)
