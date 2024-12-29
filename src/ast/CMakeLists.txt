add_library(ast OBJECT ast.c ast_free.c ast_dump.c ast_emit.c visitor.c)
target_link_libraries(ast PRIVATE haven_compiler_options)