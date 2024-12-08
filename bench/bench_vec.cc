#include <benchmark/benchmark.h>

#include <glm/glm.hpp>

extern float x, y, z;

extern "C" void test_vadd(float a, float b, float c, float d, float e, float f);
extern "C" void test_vcross(float a, float b, float c, float d, float e, float f);
extern "C" float bench_vdot(float a, float b, float c, float d, float e, float f);
extern "C" void test_vnorm(float a, float b, float c);
extern "C" void test_vscale(float a, float b, float c, float d);

static void BM_GLMDotProduct(benchmark::State& state) {
  for (auto _ : state) {
    glm::vec3 a = glm::vec3(1.0f, 2.0f, 3.0f);
    glm::vec3 b = glm::vec3(4.0f, 5.0f, 6.0f);
    benchmark::DoNotOptimize(glm::dot(a, b));
  }
}
BENCHMARK(BM_GLMDotProduct);

static void BM_MattCDotProduct(benchmark::State& state) {
  glm::vec3 a = glm::vec3(1.0f, 2.0f, 3.0f);
  glm::vec3 b = glm::vec3(4.0f, 5.0f, 6.0f);

  for (auto _ : state) {
    benchmark::DoNotOptimize(bench_vdot(a.x, a.y, a.z, b.x, b.y, b.z));
  }
}
BENCHMARK(BM_MattCDotProduct);

BENCHMARK_MAIN();
