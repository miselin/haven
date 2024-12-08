llvm_map_components_to_libnames(llvm_libs support core analysis)

add_library(mattc_lib STATIC
    lex.c
    parse.c
    codegen.c
    typecheck.c
    kv.c
    scope.c
    types.c
    ast.c
    cfold.c
)
target_link_libraries(mattc_lib PUBLIC ${llvm_libs})

add_executable(mattc
    main.c
)
target_link_libraries(mattc mattc_lib)

# Use C compiler to build, but C++ compiler for linking due to LLVM
set_target_properties(mattc PROPERTIES LINKER_LANGUAGE CXX)
