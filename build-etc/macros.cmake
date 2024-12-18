
macro(add_bootstrap_haven_library name source)

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${name}.o
        COMMAND haven_bootstrap --debug-ast --O2 ${CMAKE_CURRENT_SOURCE_DIR}/${source} -o ${CMAKE_CURRENT_BINARY_DIR}/${name}.o
        MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/${source}
        DEPENDS haven_bootstrap ${ARGN}
    )

    add_library(${name} STATIC ${CMAKE_CURRENT_BINARY_DIR}/${name}.o)
    # set link language to C
    set_target_properties(${name} PROPERTIES LINKER_LANGUAGE C)
endmacro()

macro(add_haven_library name source)

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${name}.o
        COMMAND haven --O2 ${CMAKE_CURRENT_SOURCE_DIR}/${source} -o ${CMAKE_CURRENT_BINARY_DIR}/${name}.o
        MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/${source}
        DEPENDS haven ${ARGN}
    )

    add_library(${name} STATIC ${CMAKE_CURRENT_BINARY_DIR}/${name}.o)
    # set link language to C
    set_target_properties(${name} PROPERTIES LINKER_LANGUAGE C)
endmacro()

