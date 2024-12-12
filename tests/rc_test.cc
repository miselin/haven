#include <gtest/gtest.h>

extern "C" int sut();

#ifndef EXPECTED_RC
#define EXPECTED_RC 0
#endif

TEST(CompiledFunctionRCTest, TESTNAME) {
  int rc = sut();
  EXPECT_EQ(rc, EXPECTED_RC);
}
