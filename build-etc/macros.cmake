
macro(add_mattc_library name source)

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${name}.ir
        COMMAND mattc ${CMAKE_CURRENT_SOURCE_DIR}/${source} >${CMAKE_CURRENT_BINARY_DIR}/${name}.ir
        MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/${source}
        DEPENDS mattc
    )

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${name}.ir.opt
        COMMAND ${LLVM_TOOLS_BINARY_DIR}/opt --O2 ${CMAKE_CURRENT_BINARY_DIR}/${name}.ir -o ${CMAKE_CURRENT_BINARY_DIR}/${name}.ir.opt
        MAIN_DEPENDENCY ${CMAKE_CURRENT_BINARY_DIR}/${name}.ir
        DEPENDS mattc
    )

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${name}.s
        COMMAND ${LLVM_TOOLS_BINARY_DIR}/llc -O2 -filetype=asm ${CMAKE_CURRENT_BINARY_DIR}/${name}.ir.opt -o ${CMAKE_CURRENT_BINARY_DIR}/${name}.s
        MAIN_DEPENDENCY ${CMAKE_CURRENT_BINARY_DIR}/${name}.ir.opt
    )

    add_library(${name} ${CMAKE_CURRENT_BINARY_DIR}/${name}.s)
    # set link language to C
    set_target_properties(${name} PROPERTIES LINKER_LANGUAGE C)
endmacro()
