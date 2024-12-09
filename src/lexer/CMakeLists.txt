add_library(lexer STATIC lex.c token.c stream.c diag.c int.c itype.c kw.c str.c vec.c)
target_link_libraries(lexer PUBLIC mattc_compiler_options utilities)