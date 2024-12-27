add_library(pass_typecheck OBJECT typecheck.c expr.c implicit.c resolve.c util.c)
target_link_libraries(pass_typecheck PRIVATE haven_compiler_options)
