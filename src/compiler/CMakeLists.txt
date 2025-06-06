llvm_map_components_to_libnames(llvm_libs -18)

add_library(compiler_c OBJECT compiler.c import.c flags.c search.c diag.c link.c)
target_link_libraries(compiler_c PRIVATE haven_compiler_options)

set (COMPILER_BASE_DEPS
    $<TARGET_OBJECTS:compiler_c>
    $<TARGET_OBJECTS:ast>
    $<TARGET_OBJECTS:lexer>
    $<TARGET_OBJECTS:parser>
    $<TARGET_OBJECTS:ir>
    $<TARGET_OBJECTS:cimport_c>
    $<TARGET_OBJECTS:types>
    $<TARGET_OBJECTS:tokens>

    $<TARGET_OBJECTS:pass_cfold>
    $<TARGET_OBJECTS:pass_typecheck>
    $<TARGET_OBJECTS:pass_tyverify>
    $<TARGET_OBJECTS:pass_tyimplicit>
    $<TARGET_OBJECTS:pass_semantic>
    $<TARGET_OBJECTS:pass_desugar>
)

set (COMPILER_BOOTSTRAP_DEPS
    ${COMPILER_BASE_DEPS}
    $<TARGET_OBJECTS:utilities_bootstrap>
    $<TARGET_OBJECTS:pass_purity>
)

set (COMPILER_DEPS
    ${COMPILER_BASE_DEPS}
    $<TARGET_OBJECTS:utilities>
    $<TARGET_OBJECTS:pass_haven_purity>
)

set (LLVM_LIBS libclang ${llvm_libs})

add_library(compiler_bootstrap STATIC ${COMPILER_BOOTSTRAP_DEPS})
target_link_libraries(compiler_bootstrap PUBLIC ${LLVM_LIBS})
target_link_libraries(compiler_bootstrap PRIVATE haven_compiler_options)

add_library(compiler STATIC ${COMPILER_DEPS})
target_link_libraries(compiler PUBLIC ${LLVM_LIBS} utilities_haven pass_haven_purity)
target_link_libraries(compiler PRIVATE haven_compiler_options)
