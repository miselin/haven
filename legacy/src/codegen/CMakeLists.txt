add_library(ir OBJECT codegen.c binary.c expr.c if.c utils.c fdecl.c vdecl.c match.c emit.c llvm.c preamble.c lvalue.c stmt.c)
target_link_libraries(ir PRIVATE haven_compiler_options)