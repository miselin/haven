
add_library(pass_cfold STATIC cfold.c)
target_link_libraries(pass_cfold PUBLIC ast types utilities)
target_link_libraries(pass_cfold PRIVATE mattc_compiler_options)

add_library(pass_typecheck STATIC typecheck.c)
target_link_libraries(pass_typecheck PUBLIC ast types utilities)
target_link_libraries(pass_typecheck PRIVATE mattc_compiler_options)

add_library(pass_tyverify STATIC tyverify.c)
target_link_libraries(pass_tyverify PUBLIC ast types utilities)
target_link_libraries(pass_tyverify PRIVATE mattc_compiler_options)

add_library(pass_tyimplicit STATIC tyimplicit.c)
target_link_libraries(pass_tyimplicit PUBLIC ast types utilities)
target_link_libraries(pass_tyimplicit PRIVATE mattc_compiler_options)

add_library(pass_purity STATIC purity.c)
target_link_libraries(pass_purity PUBLIC ast types utilities)
target_link_libraries(pass_purity PRIVATE mattc_compiler_options)

add_library(pass_semantic STATIC semantic.c)
target_link_libraries(pass_semantic PUBLIC ast types utilities)
target_link_libraries(pass_semantic PRIVATE mattc_compiler_options)

add_library(passes INTERFACE)
target_link_libraries(passes INTERFACE pass_cfold pass_typecheck pass_tyverify pass_tyimplicit pass_purity pass_semantic)
