add_subdirectory(codegen)

get_property(HAVEN_CODEGEN_MODULES GLOBAL PROPERTY HAVEN_CODEGEN_MODULES)

add_library(runtime_asm OBJECT syscall-amd64.s)

add_haven_runtime_library(runtime index.hv ADDITIONAL_FILES box.hv syscall.hv ${HAVEN_CODEGEN_MODULES} LIBRARY_DEPENDS runtime_asm)
add_dependencies(runtime runtime_codegen)
