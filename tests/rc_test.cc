#include <gtest/gtest.h>

int sut_rc = 0;

extern "C" int sut();

extern "C" int sut_exit(int rc) {
  sut_rc = rc;
  return rc;
}

#ifndef EXPECTED_RC
#define EXPECTED_RC 0
#endif

TEST(CompiledFunctionRCTest, TESTNAME) {
  int rc = sut();
  if (sut_rc != 0) {
    rc = sut_rc;
  }
  EXPECT_EQ(rc, EXPECTED_RC);
}
