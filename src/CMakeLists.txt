add_subdirectory(lexer)
add_subdirectory(parser)
add_subdirectory(codegen)
add_subdirectory(passes)
add_subdirectory(tokens)
add_subdirectory(ast)
add_subdirectory(utilities)
add_subdirectory(types)

add_executable(mattc
    main.c
)
target_link_libraries(mattc lexer parser ir passes)

# Use C compiler to build, but C++ compiler for linking due to LLVM
set_target_properties(mattc PROPERTIES LINKER_LANGUAGE CXX)
