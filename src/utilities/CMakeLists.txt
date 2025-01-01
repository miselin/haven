add_library(utilities_bootstrap OBJECT kv.c scope.c trie.c tokenstream.c string_builder.c)
target_link_libraries(utilities_bootstrap PRIVATE haven_compiler_options)

add_bootstrap_haven_library(utilities_haven utils.hv trie.hv kv.hv scope.hv)

add_library(utilities OBJECT tokenstream.c string_builder.c)
target_link_libraries(utilities PRIVATE haven_compiler_options)
target_link_libraries(utilities PUBLIC utilities_haven)
