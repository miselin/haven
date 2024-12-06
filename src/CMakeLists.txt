llvm_map_components_to_libnames(llvm_libs support core analysis)

add_executable(mattc
    main.c
    lex.c
    parse.c
    codegen.c
    typecheck.c
    kv.c
    scope.c
)
target_link_libraries(mattc ${llvm_libs})

# Use C compiler to build, but C++ compiler for linking due to LLVM
set_target_properties(mattc PROPERTIES LINKER_LANGUAGE CXX)
