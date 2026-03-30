#include <gtest/gtest.h>

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
extern "C" void mat_row(float3x3 *m, float3 *out, int row);

extern "C" void dump_mat(float3x3 m);

struct ref_vec3 {
  float x;
  float y;
  float z;
};

struct ref_mat3 {
  float elements[9];
};

static float3 ref2vec(ref_vec3 v) {
  float3 result = {v.x, v.y, v.z};
  return result;
}

static inline void ref2mat(ref_mat3 m, float3x3 *result) {
  *result = {m.elements[0], m.elements[1], m.elements[2], m.elements[3], m.elements[4],
             m.elements[5], m.elements[6], m.elements[7], m.elements[8]};
}

static inline float mat_get(ref_mat3 m, int col, int row) { return m.elements[(col * 3) + row]; }

static inline float vec_get(ref_vec3 v, int idx) {
  switch (idx) {
    case 0:
      return v.x;
    case 1:
      return v.y;
    default:
      return v.z;
  }
}

static inline void mat_set(ref_mat3 *m, int col, int row, float value) {
  m->elements[(col * 3) + row] = value;
}

static ref_mat3 ref_identity_mat() {
  return {{1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}};
}

static ref_mat3 ref_add(ref_mat3 a, ref_mat3 b) {
  ref_mat3 result = {};
  for (int i = 0; i < 9; i++) {
    result.elements[i] = a.elements[i] + b.elements[i];
  }
  return result;
}

static ref_mat3 ref_sub(ref_mat3 a, ref_mat3 b) {
  ref_mat3 result = {};
  for (int i = 0; i < 9; i++) {
    result.elements[i] = a.elements[i] - b.elements[i];
  }
  return result;
}

static ref_mat3 ref_scale(ref_mat3 a, float factor) {
  ref_mat3 result = {};
  for (int i = 0; i < 9; i++) {
    result.elements[i] = a.elements[i] * factor;
  }
  return result;
}

static ref_mat3 ref_mul(ref_mat3 a, ref_mat3 b) {
  ref_mat3 result = {};
  for (int col = 0; col < 3; col++) {
    for (int row = 0; row < 3; row++) {
      float sum = 0.0f;
      for (int k = 0; k < 3; k++) {
        sum += mat_get(a, k, row) * mat_get(b, col, k);
      }
      mat_set(&result, col, row, sum);
    }
  }
  return result;
}

static ref_vec3 ref_vec_mul_mat(ref_vec3 vec, ref_mat3 mat) {
  return {
      (vec.x * mat_get(mat, 0, 0)) + (vec.y * mat_get(mat, 0, 1)) + (vec.z * mat_get(mat, 0, 2)),
      (vec.x * mat_get(mat, 1, 0)) + (vec.y * mat_get(mat, 1, 1)) + (vec.z * mat_get(mat, 1, 2)),
      (vec.x * mat_get(mat, 2, 0)) + (vec.y * mat_get(mat, 2, 1)) + (vec.z * mat_get(mat, 2, 2)),
  };
}

static ref_vec3 ref_mat_column(ref_mat3 mat, int col) {
  return {mat_get(mat, col, 0), mat_get(mat, col, 1), mat_get(mat, col, 2)};
}

TEST(MatTest, Add) {
  ref_mat3 a = {{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f}};
  ref_mat3 b = ref_identity_mat();

  ref_mat3 c = ref_add(a, b);

  float3x3 a_mat;
  ref2mat(a, &a_mat);
  float3x3 b_mat;
  ref2mat(b, &b_mat);
  float3x3 result;
  madd(&result, &a_mat, &b_mat);

  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      EXPECT_EQ(result[i * 3 + j], mat_get(c, i, j));
    }
  }
}

TEST(MatTest, Sub) {
  ref_mat3 a = {{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f}};
  ref_mat3 b = ref_identity_mat();

  ref_mat3 c = ref_sub(a, b);

  float3x3 a_mat;
  ref2mat(a, &a_mat);
  float3x3 b_mat;
  ref2mat(b, &b_mat);
  float3x3 result;
  msub(&result, &a_mat, &b_mat);

  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      EXPECT_EQ(result[i * 3 + j], mat_get(c, i, j));
    }
  }
}

TEST(MatTest, Scale) {
  ref_mat3 a = {{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f}};

  ref_mat3 c = ref_scale(a, 5.0f);

  float3x3 a_mat;
  ref2mat(a, &a_mat);
  float3x3 result;
  mscale(&result, &a_mat, 5.0f);

  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      EXPECT_EQ(result[i * 3 + j], mat_get(c, i, j));
    }
  }
}

TEST(MatTest, Multiply) {
  ref_mat3 a = {{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f}};
  ref_mat3 b = ref_identity_mat();

  ref_mat3 c = ref_mul(a, b);

  float3x3 a_mat;
  ref2mat(a, &a_mat);
  float3x3 b_mat;
  ref2mat(b, &b_mat);
  float3x3 result;
  mmult(&result, &a_mat, &b_mat);

  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      EXPECT_EQ(result[i * 3 + j], mat_get(c, i, j));
    }
  }
}

TEST(MatTest, MultiplyVec) {
  ref_vec3 vec = {1.0f, 2.0f, 3.0f};
  ref_mat3 mat = ref_identity_mat();

  ref_vec3 c = ref_vec_mul_mat(vec, mat);

  float3x3 mat_haven;
  ref2mat(mat, &mat_haven);
  float3 vec_haven = ref2vec(vec);
  float3 result;
  vec_mult_mat(&result, &vec_haven, &mat_haven);

  for (int i = 0; i < 3; i++) {
    EXPECT_EQ(result[i], vec_get(c, i));
  }
}

TEST(MatTest, Row) {
  ref_mat3 a = {{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f}};

  float3x3 a_mat;
  ref2mat(a, &a_mat);

  for (int row = 0; row < 3; row++) {
    ref_vec3 expected = ref_mat_column(a, row);

    float3 result;
    mat_row(&a_mat, &result, row);

    for (int i = 0; i < 3; i++) {
      EXPECT_EQ(result[i], vec_get(expected, i));
    }
  }
}
