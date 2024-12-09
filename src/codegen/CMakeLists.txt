llvm_map_components_to_libnames(llvm_libs support core analysis)

add_library(ir STATIC codegen.c)
target_link_libraries(ir PUBLIC ${llvm_libs})