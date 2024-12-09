llvm_map_components_to_libnames(llvm_libs support core analysis)

add_library(ir STATIC codegen.c binary.c expr.c if.c utils.c fdecl.c vdecl.c match.c)
target_link_libraries(ir PUBLIC mattc_compiler_options ${llvm_libs})