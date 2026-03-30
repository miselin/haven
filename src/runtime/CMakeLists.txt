add_subdirectory(codegen)

get_property(HAVEN_CODEGEN_MODULES GLOBAL PROPERTY HAVEN_CODEGEN_MODULES)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
    (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "amd64"))
    add_library(runtime_syscall OBJECT syscall-amd64.s)
else ()
    add_library(runtime_syscall OBJECT syscall.c)
endif ()

add_haven_runtime_library(runtime index.hv ADDITIONAL_FILES box.hv syscall.hv ${HAVEN_CODEGEN_MODULES} LIBRARY_DEPENDS runtime_syscall)
add_dependencies(runtime runtime_codegen)
