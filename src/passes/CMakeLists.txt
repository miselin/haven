
add_library(pass_cfold OBJECT cfold.c)
target_link_libraries(pass_cfold PRIVATE haven_compiler_options)

add_subdirectory(typecheck)

add_library(pass_tyverify OBJECT tyverify.c)
target_link_libraries(pass_tyverify PRIVATE haven_compiler_options)

add_library(pass_tyimplicit OBJECT tyimplicit.c)
target_link_libraries(pass_tyimplicit PRIVATE haven_compiler_options)

add_library(pass_purity OBJECT purity.c)
target_link_libraries(pass_purity PRIVATE haven_compiler_options)

add_library(pass_semantic OBJECT semantic.c)
target_link_libraries(pass_semantic PRIVATE haven_compiler_options)

add_library(pass_desugar OBJECT desugar.c)
target_link_libraries(pass_desugar PRIVATE haven_compiler_options)

add_library(passes OBJECT
    $<TARGET_OBJECTS:pass_cfold>
    $<TARGET_OBJECTS:pass_typecheck>
    $<TARGET_OBJECTS:pass_tyverify>
    $<TARGET_OBJECTS:pass_tyimplicit>
    $<TARGET_OBJECTS:pass_purity>
    $<TARGET_OBJECTS:pass_semantic>
    $<TARGET_OBJECTS:pass_desugar>
)
