add_library(pass_cfold STATIC cfold.c)
add_library(pass_typecheck STATIC typecheck.c)
add_library(pass_tyverify STATIC tyverify.c)
add_library(pass_tyimplicit STATIC tyimplicit.c)
add_library(pass_purity STATIC purity.c)
target_link_libraries(pass_typecheck PUBLIC mattc_compiler_options ast types utilities)

add_library(passes INTERFACE)
target_link_libraries(passes INTERFACE mattc_compiler_options pass_cfold pass_typecheck pass_tyverify pass_tyimplicit pass_purity)
