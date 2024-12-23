add_library(utilities STATIC kv.c scope.c trie.c tokenstream.c)
target_link_libraries(utilities PRIVATE haven_compiler_options)
target_link_libraries(utilities PUBLIC lexer)

# add_haven_library(utilities_haven trie.hv)
