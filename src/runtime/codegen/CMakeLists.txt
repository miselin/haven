list(APPEND GEN_SRCS "${GEN_DIR}/gen_unistd.hv")

separate_arguments(HAVEN_BOOTSTRAP_COMPILE_FLAGS_LIST NATIVE_COMMAND ${HAVEN_BOOTSTRAP_COMPILE_FLAGS})

add_custom_command(
  OUTPUT "${GEN_DIR}/gen_unistd.hv"
  COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN_DIR}
  COMMAND $<TARGET_FILE:hdr2haven> "asm/unistd_64.h" --bootstrap ${HAVEN_BOOTSTRAP_COMPILE_FLAGS_LIST} -o "${GEN_DIR}/gen_unistd.hv"
  DEPENDS hdr2haven
  VERBATIM CODEGEN
)

add_custom_target(gen_unistd DEPENDS "${GEN_DIR}/gen_unistd.hv")
add_dependencies(runtime_codegen gen_unistd)

set_property(GLOBAL APPEND PROPERTY HAVEN_CODEGEN_MODULES "${GEN_DIR}/gen_unistd.hv")
