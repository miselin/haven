#include <gtest/gtest.h>

#include <glm/glm.hpp>

extern float x, y, z;

extern "C" void test_vadd(float a, float b, float c, float d, float e, float f);
extern "C" void test_vcross(float a, float b, float c, float d, float e, float f);
extern "C" void test_vdot(float a, float b, float c, float d, float e, float f);
extern "C" void test_vnorm(float a, float b, float c);
extern "C" void test_vscale(float a, float b, float c, float d);

TEST(VecTest, Add) {
  glm::vec3 a = glm::vec3(1.0f, 2.0f, 3.0f);
  glm::vec3 b = glm::vec3(4.0f, 5.0f, 6.0f);

  glm::vec3 c = a + b;

  EXPECT_EQ(c.x, 5.0f);
  EXPECT_EQ(c.y, 7.0f);
  EXPECT_EQ(c.z, 9.0f);

  test_vadd(1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f);

  EXPECT_EQ(x, 5.0f);
  EXPECT_EQ(y, 7.0f);
  EXPECT_EQ(z, 9.0f);
}

TEST(VecTest, Normalize) {
  glm::vec3 a = glm::vec3(1.0f, 2.0f, 3.0f);

  glm::vec3 b = glm::normalize(a);

  EXPECT_FLOAT_EQ(b.x, 0.267261236f);
  EXPECT_FLOAT_EQ(b.y, 0.534522474f);
  EXPECT_FLOAT_EQ(b.z, 0.801783681f);

  test_vnorm(1.0f, 2.0f, 3.0f);

  EXPECT_FLOAT_EQ(x, 0.267261236f);
  EXPECT_FLOAT_EQ(y, 0.534522474f);
  EXPECT_FLOAT_EQ(z, 0.801783681f);
}

TEST(VecTest, Dot) {
  glm::vec3 a = glm::vec3(1.0f, 2.0f, 3.0f);
  glm::vec3 b = glm::vec3(4.0f, 5.0f, 6.0f);

  float c = glm::dot(a, b);

  EXPECT_FLOAT_EQ(c, 32.0f);

  test_vdot(1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f);

  EXPECT_FLOAT_EQ(x, 32.0f);
}

TEST(VecTest, Cross) {
  glm::vec3 a = glm::vec3(1.0f, 2.0f, 3.0f);
  glm::vec3 b = glm::vec3(4.0f, 5.0f, 6.0f);

  glm::vec3 c = glm::cross(a, b);

  EXPECT_FLOAT_EQ(c.x, -3.0f);
  EXPECT_FLOAT_EQ(c.y, 6.0f);
  EXPECT_FLOAT_EQ(c.z, -3.0f);

  test_vcross(1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f);

  EXPECT_FLOAT_EQ(x, -3.0f);
  EXPECT_FLOAT_EQ(y, 6.0f);
  EXPECT_FLOAT_EQ(z, -3.0f);
}

TEST(VecTest, Scale) {
  glm::vec3 a = glm::vec3(1.0f, 2.0f, 3.0f);

  glm::vec3 b = a * 2.0f;

  EXPECT_FLOAT_EQ(b.x, 2.0f);
  EXPECT_FLOAT_EQ(b.y, 4.0f);
  EXPECT_FLOAT_EQ(b.z, 6.0f);

  test_vscale(1.0f, 2.0f, 3.0f, 2.0f);

  EXPECT_FLOAT_EQ(x, 2.0f);
  EXPECT_FLOAT_EQ(y, 4.0f);
  EXPECT_FLOAT_EQ(z, 6.0f);
}
