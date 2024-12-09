
add_subdirectory(inputs)

add_executable(vec_test vec_test.cc)
target_link_libraries(vec_test mattc_vec GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(vec_test)

macro(add_functional_test name expectedrc lib)
    add_executable(${name} rc_test.cc)
    target_link_libraries(${name} ${lib} GTest::gtest_main)
    target_compile_definitions(${name} PRIVATE EXPECTED_RC=${expectedrc})
    gtest_discover_tests(${name})
endmacro()

add_functional_test(simplest_test 0 test_input_simplest)
add_functional_test(add_test 6 test_input_add)
add_functional_test(prec1_test 90 test_input_prec1)
add_functional_test(prec2_test 0 test_input_prec2)

