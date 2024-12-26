llvm_map_components_to_libnames(llvm_libs -18)

add_library(ir STATIC codegen.c binary.c expr.c if.c utils.c fdecl.c vdecl.c match.c emit.c llvm.c preamble.c lvalue.c stmt.c)
target_link_libraries(ir PUBLIC ${llvm_libs})
target_link_libraries(ir PRIVATE haven_compiler_options)