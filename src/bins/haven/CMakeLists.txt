
# temporary bootstrap build that doesn't include Haven code
add_executable(haven_bootstrap main.c)
target_link_libraries(haven_bootstrap haven_compiler_options compiler_bootstrap)
set_target_properties(haven_bootstrap PROPERTIES LINKER_LANGUAGE CXX)

add_bootstrap_haven_library(haven_driver main.hv)
