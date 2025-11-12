list(APPEND GEN_SRCS "${GEN_DIR}/gen_unistd.hv")

add_custom_command(
  OUTPUT "${GEN_DIR}/gen_unistd.hv"
  COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN_DIR}
  COMMAND $<TARGET_FILE:hdr2haven> "asm/unistd_64.h" --bootstrap -I "/usr/include" -I "/usr/include/x86_64-linux-gnu" -o "${GEN_DIR}/gen_unistd.hv"
  DEPENDS hdr2haven
  VERBATIM CODEGEN
)

add_custom_target(gen_unistd DEPENDS "${GEN_DIR}/gen_unistd.hv")
add_dependencies(runtime_codegen gen_unistd)

set_property(GLOBAL APPEND PROPERTY HAVEN_CODEGEN_MODULES "${GEN_DIR}/gen_unistd.hv")
