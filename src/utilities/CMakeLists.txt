add_library(utilities STATIC kv.c scope.c trie.c)
target_link_libraries(utilities PRIVATE haven_compiler_options)