
if (ASAN)
    set (HAVEN_SANITIZER_FLAGS "--asan")
else ()
    set (HAVEN_SANITIZER_FLAGS "")
endif ()


macro(add_bootstrap_haven_library name source)
    separate_arguments(HAVEN_BOOTSTRAP_COMPILE_FLAGS_LIST NATIVE_COMMAND ${HAVEN_BOOTSTRAP_COMPILE_FLAGS})

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${name}.o
        COMMAND haven_bootstrap ${HAVEN_SANITIZER_FLAGS} --debug-ir --bootstrap -c ${HAVEN_BOOTSTRAP_COMPILE_FLAGS_LIST} ${CMAKE_CURRENT_SOURCE_DIR}/${source} -o ${CMAKE_CURRENT_BINARY_DIR}/${name}.o
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
        COMMAND haven_bootstrap ${HAVEN_SANITIZER_FLAGS} --bootstrap -c ${HAVEN_BOOTSTRAP_COMPILE_FLAGS_LIST} ${CMAKE_CURRENT_SOURCE_DIR}/${source} -o ${CMAKE_CURRENT_BINARY_DIR}/${name}.o
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
        COMMAND haven ${HAVEN_SANITIZER_FLAGS} --debug-ir -c ${HAVEN_COMPILE_FLAGS_LIST} ${CMAKE_CURRENT_SOURCE_DIR}/${source} -o ${CMAKE_CURRENT_BINARY_DIR}/${name}.o
        MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/${source}
        DEPENDS haven ${ARGN}
        COMMENT "Building ${name} from ${source}"
    )

    add_library(${name} STATIC ${CMAKE_CURRENT_BINARY_DIR}/${name}.o)
    target_link_libraries(${name} runtime)
    # set link language to C
    set_target_properties(${name} PROPERTIES LINKER_LANGUAGE C)
endmacro()

macro(add_haven_test_library name source optlevel)
    separate_arguments(HAVEN_COMPILE_FLAGS_LIST NATIVE_COMMAND ${HAVEN_COMPILE_FLAGS})

    # --debug-ir -> run a few extra IR passes + show the IR in the build output (for extra validation)
    # --O${optlevel} -> set the optimization level - tests should pass at all major optimization levels
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${name}.o
        COMMAND haven ${HAVEN_SANITIZER_FLAGS} --debug-ir --O${optlevel} -c ${HAVEN_COMPILE_FLAGS_LIST} ${CMAKE_CURRENT_SOURCE_DIR}/${source} -o ${CMAKE_CURRENT_BINARY_DIR}/${name}.o
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
        COMMAND haven_bootstrap ${HAVEN_SANITIZER_FLAGS} -c --no-preamble ${HAVEN_COMPILE_FLAGS_LIST} ${CMAKE_CURRENT_SOURCE_DIR}/${source} -o ${CMAKE_CURRENT_BINARY_DIR}/${name}.o
        MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/${source}
        DEPENDS haven_bootstrap ${ARGN}
        COMMENT "Building ${name} from ${source}"
    )

    add_library(${name} STATIC ${CMAKE_CURRENT_BINARY_DIR}/${name}.o)
    # set link language to C
    set_target_properties(${name} PROPERTIES LINKER_LANGUAGE C)
endmacro()

