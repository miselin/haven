# final compiler build including Haven code
add_bootstrap_haven_executable(haven-selfhost main.hv)
target_link_libraries(haven-selfhost haven_compiler_options)

# Use C compiler to build, but C++ compiler for linking due to LLVM
set_target_properties(haven-selfhost PROPERTIES LINKER_LANGUAGE CXX)
