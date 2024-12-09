add_library(ast STATIC ast.c ast_free.c ast_dump.c)
target_link_libraries(ast PUBLIC mattc_compiler_options)