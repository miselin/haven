#include <gtest/gtest.h>
#include <cmath>

// actually a vec4, due to alignment, but we only use the first 3 components
typedef float float3 __attribute__((vector_size(sizeof(float) * 4)));

extern "C" float3 vadd(float3 a, float3 b);
extern "C" float3 vcross(float3 a, float3 b);
extern "C" float vdot(float3 a, float3 b);
extern "C" float3 vnorm(float3 a);
extern "C" float3 vscale(float3 a, float s);
extern "C" float velement(float3 v, int idx);
extern "C" float3 make_fvec3(float x, float y, float z);
extern "C" float3 make_const_fvec3();

struct ref_vec3 {
  float x;
  float y;
  float z;
};

static float ref_get(ref_vec3 v, int idx) {
  switch (idx) {
    case 0:
      return v.x;
    case 1:
      return v.y;
    default:
      return v.z;
  }
}

static float3 ref2vec(ref_vec3 v) {
  float3 result = {v.x, v.y, v.z, 0.0f};
  return result;
}

static ref_vec3 ref_add(ref_vec3 a, ref_vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }

static float ref_dot(ref_vec3 a, ref_vec3 b) {
  return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

static ref_vec3 ref_cross(ref_vec3 a, ref_vec3 b) {
  return {
      (a.y * b.z) - (a.z * b.y),
      (a.z * b.x) - (a.x * b.z),
      (a.x * b.y) - (a.y * b.x),
  };
}

static ref_vec3 ref_scale(ref_vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }

static ref_vec3 ref_normalize(ref_vec3 a) {
  float len = std::sqrt(ref_dot(a, a));
  return {a.x / len, a.y / len, a.z / len};
}

TEST(VecTest, Add) {
  ref_vec3 a = {1.0f, 2.0f, 3.0f};
  ref_vec3 b = {4.0f, 5.0f, 6.0f};

  ref_vec3 c = ref_add(a, b);

  EXPECT_EQ(c.x, 5.0f);
  EXPECT_EQ(c.y, 7.0f);
  EXPECT_EQ(c.z, 9.0f);

  float3 result = vadd(ref2vec(a), ref2vec(b));

  EXPECT_EQ(result[0], 5.0f);
  EXPECT_EQ(result[1], 7.0f);
  EXPECT_EQ(result[2], 9.0f);
}

TEST(VecTest, Normalize) {
  ref_vec3 a = {1.0f, 2.0f, 3.0f};

  ref_vec3 b = ref_normalize(a);

  EXPECT_FLOAT_EQ(b.x, 0.267261236f);
  EXPECT_FLOAT_EQ(b.y, 0.534522474f);
  EXPECT_FLOAT_EQ(b.z, 0.801783681f);

  float3 result = vnorm(ref2vec(a));

  EXPECT_FLOAT_EQ(result[0], 0.267261236f);
  EXPECT_FLOAT_EQ(result[1], 0.534522474f);
  EXPECT_FLOAT_EQ(result[2], 0.801783681f);
}

TEST(VecTest, Dot) {
  ref_vec3 a = {1.0f, 2.0f, 3.0f};
  ref_vec3 b = {4.0f, 5.0f, 6.0f};

  float c = ref_dot(a, b);

  EXPECT_FLOAT_EQ(c, 32.0f);

  float result = vdot(ref2vec(a), ref2vec(b));

  EXPECT_FLOAT_EQ(result, 32.0f);
}

TEST(VecTest, Cross) {
  ref_vec3 a = {1.0f, 2.0f, 3.0f};
  ref_vec3 b = {4.0f, 5.0f, 6.0f};

  ref_vec3 c = ref_cross(a, b);

  EXPECT_FLOAT_EQ(c.x, -3.0f);
  EXPECT_FLOAT_EQ(c.y, 6.0f);
  EXPECT_FLOAT_EQ(c.z, -3.0f);

  float3 result = vcross(ref2vec(a), ref2vec(b));

  EXPECT_FLOAT_EQ(result[0], -3.0f);
  EXPECT_FLOAT_EQ(result[1], 6.0f);
  EXPECT_FLOAT_EQ(result[2], -3.0f);
}

TEST(VecTest, Scale) {
  ref_vec3 a = {1.0f, 2.0f, 3.0f};

  ref_vec3 b = ref_scale(a, 2.0f);

  EXPECT_FLOAT_EQ(b.x, 2.0f);
  EXPECT_FLOAT_EQ(b.y, 4.0f);
  EXPECT_FLOAT_EQ(b.z, 6.0f);

  float3 result = vscale(ref2vec(a), 2.0f);

  EXPECT_FLOAT_EQ(result[0], 2.0f);
  EXPECT_FLOAT_EQ(result[1], 4.0f);
  EXPECT_FLOAT_EQ(result[2], 6.0f);
}

TEST(VecTest, Element) {
  ref_vec3 a = {1.0f, 2.0f, 3.0f};

  EXPECT_FLOAT_EQ(ref_get(a, 0), 1.0f);
  EXPECT_FLOAT_EQ(ref_get(a, 1), 2.0f);
  EXPECT_FLOAT_EQ(ref_get(a, 2), 3.0f);

  float3 vec = ref2vec(a);

  EXPECT_FLOAT_EQ(velement(vec, 0), 1.0f);
  EXPECT_FLOAT_EQ(velement(vec, 1), 2.0f);
  EXPECT_FLOAT_EQ(velement(vec, 2), 3.0f);
}

TEST(VecTest, MakeFVec3) {
  float3 vec = make_fvec3(7.0f, 8.0f, 9.0f);

  EXPECT_FLOAT_EQ(vec[0], 7.0f);
  EXPECT_FLOAT_EQ(vec[1], 8.0f);
  EXPECT_FLOAT_EQ(vec[2], 9.0f);
}

TEST(VecTest, MakeConstFVec3) {
  float3 vec = make_const_fvec3();

  EXPECT_FLOAT_EQ(vec[0], 1.0f);
  EXPECT_FLOAT_EQ(vec[1], 2.0f);
  EXPECT_FLOAT_EQ(vec[2], 3.0f);
}
