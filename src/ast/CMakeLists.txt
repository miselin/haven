add_library(ast STATIC ast.c ast_free.c ast_dump.c ast_emit.c)
target_link_libraries(ast PRIVATE haven_compiler_options)