add_library(lexer STATIC lex.c token.c stream.c diag.c int.c itype.c kw.c str.c vec.c)
target_link_libraries(lexer PUBLIC utilities)
target_link_libraries(lexer PRIVATE mattc_compiler_options)