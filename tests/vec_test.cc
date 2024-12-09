#include <gtest/gtest.h>

#include <glm/glm.hpp>

typedef float float3 __attribute__((vector_size(sizeof(float) * 3)));

extern "C" float3 vadd(float3 a, float3 b);
extern "C" float3 vcross(float3 a, float3 b);
extern "C" float vdot(float3 a, float3 b);
extern "C" float3 vnorm(float3 a);
extern "C" float3 vscale(float3 a, float s);

static float3 glm2vec(glm::vec3 v) {
  return {v.x, v.y, v.z};
}

TEST(VecTest, Add) {
  glm::vec3 a = glm::vec3(1.0f, 2.0f, 3.0f);
  glm::vec3 b = glm::vec3(4.0f, 5.0f, 6.0f);

  glm::vec3 c = a + b;

  EXPECT_EQ(c.x, 5.0f);
  EXPECT_EQ(c.y, 7.0f);
  EXPECT_EQ(c.z, 9.0f);

  float3 result = vadd(glm2vec(a), glm2vec(b));

  EXPECT_EQ(result[0], 5.0f);
  EXPECT_EQ(result[1], 7.0f);
  EXPECT_EQ(result[2], 9.0f);
}

TEST(VecTest, Normalize) {
  glm::vec3 a = glm::vec3(1.0f, 2.0f, 3.0f);

  glm::vec3 b = glm::normalize(a);

  EXPECT_FLOAT_EQ(b.x, 0.267261236f);
  EXPECT_FLOAT_EQ(b.y, 0.534522474f);
  EXPECT_FLOAT_EQ(b.z, 0.801783681f);

  float3 result = vnorm(glm2vec(a));

  EXPECT_FLOAT_EQ(result[0], 0.267261236f);
  EXPECT_FLOAT_EQ(result[1], 0.534522474f);
  EXPECT_FLOAT_EQ(result[2], 0.801783681f);
}

TEST(VecTest, Dot) {
  glm::vec3 a = glm::vec3(1.0f, 2.0f, 3.0f);
  glm::vec3 b = glm::vec3(4.0f, 5.0f, 6.0f);

  float c = glm::dot(a, b);

  EXPECT_FLOAT_EQ(c, 32.0f);

  float result = vdot(glm2vec(a), glm2vec(b));

  EXPECT_FLOAT_EQ(result, 32.0f);
}

TEST(VecTest, Cross) {
  glm::vec3 a = glm::vec3(1.0f, 2.0f, 3.0f);
  glm::vec3 b = glm::vec3(4.0f, 5.0f, 6.0f);

  glm::vec3 c = glm::cross(a, b);

  EXPECT_FLOAT_EQ(c.x, -3.0f);
  EXPECT_FLOAT_EQ(c.y, 6.0f);
  EXPECT_FLOAT_EQ(c.z, -3.0f);

  float3 result = vcross(glm2vec(a), glm2vec(b));

  EXPECT_FLOAT_EQ(result[0], -3.0f);
  EXPECT_FLOAT_EQ(result[1], 6.0f);
  EXPECT_FLOAT_EQ(result[2], -3.0f);
}

TEST(VecTest, Scale) {
  glm::vec3 a = glm::vec3(1.0f, 2.0f, 3.0f);

  glm::vec3 b = a * 2.0f;

  EXPECT_FLOAT_EQ(b.x, 2.0f);
  EXPECT_FLOAT_EQ(b.y, 4.0f);
  EXPECT_FLOAT_EQ(b.z, 6.0f);

  float3 result = vscale(glm2vec(a), 2.0f);

  EXPECT_FLOAT_EQ(result[0], 2.0f);
  EXPECT_FLOAT_EQ(result[1], 4.0f);
  EXPECT_FLOAT_EQ(result[2], 6.0f);
}
