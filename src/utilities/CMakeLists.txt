add_library(utilities_bootstrap STATIC kv.c scope.c trie.c tokenstream.c)
target_link_libraries(utilities_bootstrap PRIVATE haven_compiler_options)

add_bootstrap_haven_library(utilities_haven utils.hv trie.hv kv.hv)

add_library(utilities STATIC scope.c tokenstream.c)
target_link_libraries(utilities PRIVATE haven_compiler_options)
target_link_libraries(utilities PUBLIC utilities_haven)
