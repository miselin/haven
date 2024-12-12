
macro(add_mattc_library name source)

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${name}.ll
        COMMAND mattc ${CMAKE_CURRENT_SOURCE_DIR}/${source} >${CMAKE_CURRENT_BINARY_DIR}/${name}.ll
        MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/${source}
        DEPENDS mattc
    )

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${name}.s
        COMMAND ${LLVM_TOOLS_BINARY_DIR}/clang -S ${CMAKE_C_FLAGS} -O2 ${CMAKE_CURRENT_BINARY_DIR}/${name}.ll -o ${CMAKE_CURRENT_BINARY_DIR}/${name}.s
        MAIN_DEPENDENCY ${CMAKE_CURRENT_BINARY_DIR}/${name}.ll
        DEPENDS mattc
    )

    add_library(${name} ${CMAKE_CURRENT_BINARY_DIR}/${name}.s)
    # set link language to C
    set_target_properties(${name} PROPERTIES LINKER_LANGUAGE C)
endmacro()
