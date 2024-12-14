
add_executable(havenfmt havenfmt.c)
target_link_libraries(havenfmt haven_compiler_options compiler)

# Use C compiler to build, but C++ compiler for linking due to LLVM
set_target_properties(havenfmt PROPERTIES LINKER_LANGUAGE CXX)
