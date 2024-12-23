#include <benchmark/benchmark.h>

#include <glm/glm.hpp>

extern float x, y, z;

static glm::vec3 a = glm::vec3(1.0f, 2.0f, 3.0f);
static glm::vec3 b = glm::vec3(4.0f, 5.0f, 6.0f);

static glm::mat3 mat_a = glm::mat3(glm::vec3(1.0f, 2.0f, 3.0f), glm::vec3(4.0f, 5.0f, 6.0f),
                                   glm::vec3(7.0f, 8.0f, 9.0f));
static glm::mat3 mat_b = glm::mat3(glm::vec3(7.0f, 8.0f, 9.0f), glm::vec3(4.0f, 5.0f, 6.0f),
                                   glm::vec3(1.0f, 2.0f, 3.0f));

typedef float float3 __attribute__((vector_size(sizeof(float) * 3)));
typedef float float3x3 __attribute__((vector_size(sizeof(float) * 3 * 3)));

static float3 av = {1.0f, 2.0f, 3.0f};
static float3 bv = {4.0f, 5.0f, 6.0f};

static float3x3 av_mat = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f};
static float3x3 bv_mat = {7.0f, 8.0f, 9.0f, 4.0f, 5.0f, 6.0f, 1.0f, 2.0f, 3.0f};

extern "C" void madd(float3x3 *out, float3x3 *a, float3x3 *b);
extern "C" void mmult(float3x3 *out, float3x3 *a, float3x3 *b);
extern "C" void vec_mult_mat(float3 *out, float3 *a, float3x3 *b);

static void BM_GLMMatrixAdd(benchmark::State &state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(mat_a + mat_b);
  }
}

static void BM_GLMMatrixMultiply(benchmark::State &state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(mat_a * mat_b);
  }
}

static void BM_GLMMatrixMultiplyVec(benchmark::State &state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(a * mat_b);
  }
}

static void BM_HavenMatrixAdd(benchmark::State &state) {
  float3x3 result;
  for (auto _ : state) {
    madd(&result, &av_mat, &bv_mat);
    benchmark::DoNotOptimize(result);
  }
}

static void BM_HavenMatrixMultiply(benchmark::State &state) {
  float3x3 result;
  for (auto _ : state) {
    mmult(&result, &av_mat, &bv_mat);
    benchmark::DoNotOptimize(result);
  }
}

static void BM_HavenMatrixMultiplyVec(benchmark::State &state) {
  float3 result;
  for (auto _ : state) {
    vec_mult_mat(&result, &av, &bv_mat);
    benchmark::DoNotOptimize(result);
  }
}

BENCHMARK(BM_GLMMatrixAdd);
BENCHMARK(BM_GLMMatrixMultiply);
BENCHMARK(BM_GLMMatrixMultiplyVec);

BENCHMARK(BM_HavenMatrixAdd);
BENCHMARK(BM_HavenMatrixMultiply);
BENCHMARK(BM_HavenMatrixMultiplyVec);

BENCHMARK_MAIN();
