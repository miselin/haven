add_library(lexer OBJECT lex.c token.c stream.c diag.c int.c itype.c kw.c str.c vec.c comment.c mat.c)
target_link_libraries(lexer PRIVATE haven_compiler_options)