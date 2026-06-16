#include <benchmark/benchmark.h>

#include <forge/vector.hpp>

#include "bench_utils.hpp"

#include <vector>

// =================================================================================
// Pop Back
// =================================================================================

template <typename Container> static void pop_back(benchmark::State& state)
{
    const auto N = state.range(0);

    for (auto _ : state)
    {
        // Setup prefilled container to isolate pop_back cost from construction
        state.PauseTiming();
        Container vec;
        vec.reserve(N);
        for (int i = 0; i < N; ++i)
        {
            vec.push_back(typename Container::value_type(i));
        }
        state.ResumeTiming();

        // Measure cost of popping all elements
        for (int i = 0; i < N; ++i)
        {
            vec.pop_back();
        }

        benchmark::DoNotOptimize(vec.data());
        benchmark::ClobberMemory();
    }

    state.SetComplexityN(N);
}

BENCHMARK(pop_back<forge::vector<int>>)
    ->RangeMultiplier(4)
    ->Range(128, 131072)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

BENCHMARK(pop_back<std::vector<int>>)
    ->RangeMultiplier(4)
    ->Range(128, 131072)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

BENCHMARK(pop_back<forge::vector<BigObject>>)
    ->RangeMultiplier(4)
    ->Range(128, 131072)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

BENCHMARK(pop_back<std::vector<BigObject>>)
    ->RangeMultiplier(4)
    ->Range(128, 131072)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

// =================================================================================
// Clear
// =================================================================================

template <typename Container> static void clear(benchmark::State& state)
{
    const auto N = state.range(0);
    Container vec;
    vec.reserve(N);

    for (auto _ : state)
    {
        // Setup prefilled container to isolate clear cost from construction
        state.PauseTiming();
        for (int i = 0; i < N; ++i)
        {
            vec.push_back(typename Container::value_type(i));
        }
        state.ResumeTiming();

        // Measure cost of clearing all elements
        vec.clear();

        benchmark::DoNotOptimize(vec.data());
        benchmark::ClobberMemory();
    }

    state.SetComplexityN(N);
}

BENCHMARK(clear<forge::vector<int>>)
    ->RangeMultiplier(4)
    ->Range(128, 16384)
    ->Complexity(benchmark::o1)
    ->Unit(benchmark::kNanosecond)
    ->Iterations(10000);

BENCHMARK(clear<std::vector<int>>)
    ->RangeMultiplier(4)
    ->Range(128, 16384)
    ->Complexity(benchmark::o1)
    ->Unit(benchmark::kNanosecond)
    ->Iterations(10000);

BENCHMARK(clear<forge::vector<BigObject>>)
    ->RangeMultiplier(4)
    ->Range(128, 16384)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond)
    ->Iterations(5000);

BENCHMARK(clear<std::vector<BigObject>>)
    ->RangeMultiplier(4)
    ->Range(128, 16384)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond)
    ->Iterations(5000);