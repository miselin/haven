
include(GoogleTest)

add_subdirectory(inputs)

add_executable(vec_test vec_test.cc)
target_link_libraries(vec_test mattc_compiler_options_no_warnings mattc_vec GTest::gtest_main)
gtest_discover_tests(vec_test)

add_executable(trie_test trie_test.cc)
target_link_libraries(trie_test mattc_compiler_options_no_warnings utilities GTest::gtest_main)
gtest_discover_tests(trie_test)

add_executable(kw_test kw_test.cc)
target_link_libraries(kw_test mattc_compiler_options_no_warnings lexer utilities GTest::gtest_main)
gtest_discover_tests(kw_test)

macro(add_functional_test name expectedrc lib)
    add_executable(${name} rc_test.cc)
    target_link_libraries(${name} mattc_compiler_options_no_warnings ${lib} GTest::gtest_main)
    target_compile_definitions(${name} PRIVATE EXPECTED_RC=${expectedrc} TESTNAME=${name})
    gtest_discover_tests(${name})
endmacro()

add_functional_test(simplest_test 0 test_input_simplest)
add_functional_test(add_test 6 test_input_add)
add_functional_test(prec1_test 90 test_input_prec1)
add_functional_test(prec2_test 0 test_input_prec2)
add_functional_test(shortcircuit_test 1 test_input_shortcircuit)