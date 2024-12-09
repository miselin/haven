#include <benchmark/benchmark.h>

#include <glm/glm.hpp>

extern float x, y, z;

static glm::vec3 a = glm::vec3(1.0f, 2.0f, 3.0f);
static glm::vec3 b = glm::vec3(4.0f, 5.0f, 6.0f);

typedef float float3 __attribute__((vector_size(sizeof(float) * 3)));

static float3 av = {1.0f, 2.0f, 3.0f};
static float3 bv = {4.0f, 5.0f, 6.0f};

extern "C" float3 vadd(float3 a, float3 b);
extern "C" float3 vcross(float3 a, float3 b);
extern "C" float vdot(float3 a, float3 b);
extern "C" float3 vnorm(float3 a);
extern "C" float3 vscale(float3 a, float s);

static void BM_GLMAdd(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(a + b);
  }
}

static void BM_GLMCrossProduct(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(glm::cross(a, b));
  }
}

static void BM_GLMDotProduct(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(glm::dot(a, b));
  }
}

static void BM_GLMNormalize(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(glm::normalize(a));
  }
}

static void BM_GLMScale(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(a * 5.0f);
  }
}

static void BM_MattCAdd(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(vadd(av, bv));
  }
}

static void BM_MattCCrossProduct(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(vcross(av, bv));
  }
}

static void BM_MattCDotProduct(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(vdot(av, bv));
  }
}

static void BM_MattCNormalize(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(vnorm(av));
  }
}

static void BM_MattCScale(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(vscale(av, 5.0f));
  }
}

BENCHMARK(BM_GLMAdd);
BENCHMARK(BM_GLMCrossProduct);
BENCHMARK(BM_GLMDotProduct);
BENCHMARK(BM_GLMNormalize);
BENCHMARK(BM_GLMScale);
BENCHMARK(BM_MattCAdd);
BENCHMARK(BM_MattCCrossProduct);
BENCHMARK(BM_MattCDotProduct);
BENCHMARK(BM_MattCNormalize);
BENCHMARK(BM_MattCScale);

BENCHMARK_MAIN();
