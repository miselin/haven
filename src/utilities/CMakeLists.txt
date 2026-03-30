add_library(utilities_bootstrap OBJECT kv.c scope.c trie.c tokenstream.c string_builder.c)
target_link_libraries(utilities_bootstrap PRIVATE haven_compiler_options)

add_library(utilities OBJECT tokenstream.c string_builder.c)
target_link_libraries(utilities PRIVATE haven_compiler_options)

if (NOT APPLE)
    add_bootstrap_haven_library(utilities_haven utils.hv trie.hv kv.hv scope.hv hashmap.hv intern.hv)
    target_link_libraries(utilities PUBLIC utilities_haven)
endif ()
