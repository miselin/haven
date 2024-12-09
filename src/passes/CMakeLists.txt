add_library(pass_cfold STATIC cfold.c)
add_library(pass_typecheck STATIC typecheck.c)
target_link_libraries(pass_typecheck PUBLIC ast types utilities)

add_library(passes INTERFACE)
target_link_libraries(passes INTERFACE pass_cfold pass_typecheck)
