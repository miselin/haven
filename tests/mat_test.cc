#include <gtest/gtest.h>

#include <glm/glm.hpp>

// actually a vec4, due to alignment, but we only use the first 3 components
typedef float float3 __attribute__((vector_size(sizeof(float) * 3)));

// actually a mat3x3
typedef float float3x3 __attribute__((vector_size(sizeof(float) * 3 * 3)));

// the C ABI for vector_size is a bit wonky, so we're using pointers for this.
extern "C" void madd(float3x3 *out, float3x3 *a, float3x3 *b);
extern "C" void msub(float3x3 *out, float3x3 *a, float3x3 *b);
extern "C" void mmult(float3x3 *out, float3x3 *a, float3x3 *b);
extern "C" void mscale(float3x3 *out, float3x3 *a, float factor);
extern "C" void vec_mult_mat(float3 *out, float3 *a, float3x3 *b);

extern "C" void dump_mat(float3x3 m);

static float3 glm2vec(glm::vec3 v) {
  float3 result = {v.x, v.y, v.z};
  return result;
}

static inline void glm2mat(glm::mat3 m, float3x3 *result) {
  *result = {m[0][0], m[0][1], m[0][2], m[1][0], m[1][1], m[1][2], m[2][0], m[2][1], m[2][2]};
}

TEST(MatTest, Add) {
  glm::mat3 a = glm::mat3(glm::vec3(1.0f, 2.0f, 3.0f), glm::vec3(4.0f, 5.0f, 6.0f),
                          glm::vec3(7.0f, 8.0f, 9.0f));
  glm::mat3 b = glm::mat3(1.0f);

  glm::mat3 c = a + b;

  float3x3 a_mat;
  glm2mat(a, &a_mat);
  float3x3 b_mat;
  glm2mat(b, &b_mat);
  float3x3 result;
  madd(&result, &a_mat, &b_mat);

  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      EXPECT_EQ(result[i * 3 + j], c[i][j]);
    }
  }
}

TEST(MatTest, Sub) {
  glm::mat3 a = glm::mat3(glm::vec3(1.0f, 2.0f, 3.0f), glm::vec3(4.0f, 5.0f, 6.0f),
                          glm::vec3(7.0f, 8.0f, 9.0f));
  glm::mat3 b = glm::mat3(1.0f);

  glm::mat3 c = a - b;

  float3x3 a_mat;
  glm2mat(a, &a_mat);
  float3x3 b_mat;
  glm2mat(b, &b_mat);
  float3x3 result;
  msub(&result, &a_mat, &b_mat);

  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      EXPECT_EQ(result[i * 3 + j], c[i][j]);
    }
  }
}

TEST(MatTest, Scale) {
  glm::mat3 a = glm::mat3(glm::vec3(1.0f, 2.0f, 3.0f), glm::vec3(4.0f, 5.0f, 6.0f),
                          glm::vec3(7.0f, 8.0f, 9.0f));

  glm::mat3 c = a * 5.0f;

  float3x3 a_mat;
  glm2mat(a, &a_mat);
  float3x3 result;
  mscale(&result, &a_mat, 5.0f);

  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      EXPECT_EQ(result[i * 3 + j], c[i][j]);
    }
  }
}

TEST(MatTest, Multiply) {
  glm::mat3 a = glm::mat3(glm::vec3(1.0f, 2.0f, 3.0f), glm::vec3(4.0f, 5.0f, 6.0f),
                          glm::vec3(7.0f, 8.0f, 9.0f));
  glm::mat3 b = glm::mat3(1.0f);

  glm::mat3 c = a * b;

  float3x3 a_mat;
  glm2mat(a, &a_mat);
  float3x3 b_mat;
  glm2mat(b, &b_mat);
  float3x3 result;
  mmult(&result, &a_mat, &b_mat);

  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      EXPECT_EQ(result[i * 3 + j], c[i][j]);
    }
  }
}

TEST(MatTest, MultiplyVec) {
  glm::vec3 vec = glm::vec3(1.0f, 2.0f, 3.0f);
  glm::mat3 mat = glm::mat3(1.0f);

  glm::vec3 c = vec * mat;

  float3x3 mat_haven;
  glm2mat(mat, &mat_haven);
  float3 vec_haven = glm2vec(vec);
  float3 result;
  vec_mult_mat(&result, &vec_haven, &mat_haven);

  for (int i = 0; i < 3; i++) {
    EXPECT_EQ(result[i], c[i]);
  }
}
