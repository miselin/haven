
include(GoogleTest)

add_mattc_library(mattc_vec inputs/vec.mc)

add_executable(vec_test vec_test.cc)
target_link_libraries(vec_test mattc_compiler_options_no_warnings mattc_vec GTest::gtest_main)
gtest_discover_tests(vec_test)

add_executable(trie_test trie_test.cc)
target_link_libraries(trie_test mattc_compiler_options_no_warnings compiler GTest::gtest_main)
gtest_discover_tests(trie_test)

add_executable(kw_test kw_test.cc)
target_link_libraries(kw_test mattc_compiler_options_no_warnings compiler GTest::gtest_main)
gtest_discover_tests(kw_test)

macro(add_functional_test name expectedrc)
    add_executable(${name}_test rc_test.cc)
    add_mattc_library(${name}_sut inputs/${name}.mc)
    target_link_libraries(${name}_test mattc_compiler_options_no_warnings ${name}_sut GTest::gtest_main)
    target_compile_definitions(${name}_test PRIVATE EXPECTED_RC=${expectedrc} TESTNAME=${name})
    gtest_discover_tests(${name}_test)
endmacro()

add_functional_test(simplest 0)
add_functional_test(add 6)
add_functional_test(prec1 90)
add_functional_test(prec2 0)
add_functional_test(shortcircuit 1)
add_functional_test(constant 0)
# asan error
# add_functional_test(struct 3)
add_functional_test(match 0)
add_functional_test(enum 2)
add_functional_test(enum_nobind 0)
add_functional_test(ptr 5)
add_functional_test(array 16)
add_functional_test(defer 1)
add_functional_test(type_alias 0)
# fails, need to fix
# add_functional_test(block 0)
add_functional_test(iter_up 0)
add_functional_test(iter_down 0)
# structs cause asan error at the moment
# add_functional_test(llist 1)