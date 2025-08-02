
macro(add_bootstrap_haven_library name source)
    separate_arguments(HAVEN_BOOTSTRAP_COMPILE_FLAGS_LIST NATIVE_COMMAND ${HAVEN_BOOTSTRAP_COMPILE_FLAGS})

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${name}.o
        COMMAND haven_bootstrap --bootstrap -c ${HAVEN_BOOTSTRAP_COMPILE_FLAGS_LIST} ${CMAKE_CURRENT_SOURCE_DIR}/${source} -o ${CMAKE_CURRENT_BINARY_DIR}/${name}.o
        MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/${source}
        DEPENDS haven_bootstrap runtime ${ARGN}
        COMMENT "Building ${name} from ${source} [bootstrap]"
    )

    add_library(${name} STATIC ${CMAKE_CURRENT_BINARY_DIR}/${name}.o)
    target_link_libraries(${name} runtime)
    # set link language to C
    set_target_properties(${name} PROPERTIES LINKER_LANGUAGE C)
endmacro()

macro(add_bootstrap_haven_executable name source)
    separate_arguments(HAVEN_BOOTSTRAP_COMPILE_FLAGS_LIST NATIVE_COMMAND ${HAVEN_BOOTSTRAP_COMPILE_FLAGS})

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${name}.o
        COMMAND haven_bootstrap --bootstrap -c ${HAVEN_BOOTSTRAP_COMPILE_FLAGS_LIST} ${CMAKE_CURRENT_SOURCE_DIR}/${source} -o ${CMAKE_CURRENT_BINARY_DIR}/${name}.o
        MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/${source}
        DEPENDS haven_bootstrap runtime ${ARGN}
        COMMENT "Building ${name} from ${source} [bootstrap]"
    )

    add_executable(${name} ${CMAKE_CURRENT_BINARY_DIR}/${name}.o)
    target_link_libraries(${name} runtime)
    # set link language to C
    set_target_properties(${name} PROPERTIES LINKER_LANGUAGE C)
endmacro()

macro(add_haven_library name source)
    separate_arguments(HAVEN_COMPILE_FLAGS_LIST NATIVE_COMMAND ${HAVEN_COMPILE_FLAGS})

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${name}.o
        COMMAND haven -c ${HAVEN_COMPILE_FLAGS_LIST} ${CMAKE_CURRENT_SOURCE_DIR}/${source} -o ${CMAKE_CURRENT_BINARY_DIR}/${name}.o
        MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/${source}
        DEPENDS haven ${ARGN}
        COMMENT "Building ${name} from ${source}"
    )

    add_library(${name} STATIC ${CMAKE_CURRENT_BINARY_DIR}/${name}.o)
    target_link_libraries(${name} runtime)
    # set link language to C
    set_target_properties(${name} PROPERTIES LINKER_LANGUAGE C)
endmacro()

macro(add_haven_runtime_library name source)
    separate_arguments(HAVEN_COMPILE_FLAGS_LIST NATIVE_COMMAND ${HAVEN_COMPILE_FLAGS})

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${name}.o
        COMMAND haven_bootstrap -c --no-preamble ${HAVEN_COMPILE_FLAGS_LIST} ${CMAKE_CURRENT_SOURCE_DIR}/${source} -o ${CMAKE_CURRENT_BINARY_DIR}/${name}.o
        MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/${source}
        DEPENDS haven_bootstrap ${ARGN}
        COMMENT "Building ${name} from ${source}"
    )

    add_library(${name} STATIC ${CMAKE_CURRENT_BINARY_DIR}/${name}.o)
    # set link language to C
    set_target_properties(${name} PROPERTIES LINKER_LANGUAGE C)
endmacro()

