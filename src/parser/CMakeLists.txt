add_library(parser STATIC parse.c)
target_link_libraries(parser PUBLIC ast tokens)