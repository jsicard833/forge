#include <benchmark/benchmark.h>

#include <forge/vector.hpp>

#include "bench_utils.hpp"

#include <cstddef>
#include <numeric>
#include <random>
#include <type_traits>
#include <vector>

// =================================================================================
// Sequential Iteration
// =================================================================================

template <typename Container> static void sequential_iteration(benchmark::State& state)
{
    const auto N = state.range(0);
    Container vec(N, typename Container::value_type(1)); // Pre-size the container

    for (auto _ : state)
    {
        long long sum = 0;

        // The core test loop
        for (const auto& item : vec)
        {
            if constexpr (std::is_same_v<typename Container::value_type, int>)
            {
                sum += item;
            }
            else
            {
                sum += item.data[0]; // For BigObject, just grab the first element
            }
        }

        benchmark::DoNotOptimize(sum);
    }

    state.SetComplexityN(N);
}

BENCHMARK(sequential_iteration<forge::vector<int>>)
    ->RangeMultiplier(4)
    ->Range(1024, 1 << 22)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

BENCHMARK(sequential_iteration<std::vector<int>>)
    ->RangeMultiplier(4)
    ->Range(1024, 1 << 22)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

BENCHMARK(sequential_iteration<forge::vector<BigObject>>)
    ->RangeMultiplier(4)
    ->Range(64, 16384)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

BENCHMARK(sequential_iteration<std::vector<BigObject>>)
    ->RangeMultiplier(4)
    ->Range(64, 16384)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

// =================================================================================
// Random Access
// =================================================================================

template <typename Container> static void random_access(benchmark::State& state)
{
    const auto N = state.range(0);

    Container vec(N); // Pre-size the container
    // Fill it with dummy data
    for (size_t i = 0; i < N; ++i)
    {
        if constexpr (std::is_same_v<typename Container::value_type, int>)
        {
            vec[i] = static_cast<int>(i);
        }
        else
        {
            vec[i].data[0] = static_cast<int>(i);
        }
    }

    std::vector<size_t> random_indices(N);
    std::iota(random_indices.begin(), random_indices.end(), 0);
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(random_indices.begin(), random_indices.end(), g);

    for (auto _ : state)
    {
        long long sum = 0;

        for (size_t idx : random_indices)
        {
            if constexpr (std::is_same_v<typename Container::value_type, int>)
            {
                sum += vec[idx];
            }
            else
            {
                sum += vec[idx].data[0]; // For BigObject, just grab the first element
            }
        }

        benchmark::DoNotOptimize(sum);
    }

    state.SetComplexityN(N);
}

BENCHMARK(random_access<forge::vector<int>>)
    ->RangeMultiplier(4)
    ->Range(1024, 1 << 22)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

BENCHMARK(random_access<std::vector<int>>)
    ->RangeMultiplier(4)
    ->Range(1024, 1 << 22)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

BENCHMARK(random_access<forge::vector<BigObject>>)
    ->RangeMultiplier(4)
    ->Range(64, 16384)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

BENCHMARK(random_access<std::vector<BigObject>>)
    ->RangeMultiplier(4)
    ->Range(64, 16384)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

// =================================================================================
// operator[] vs at()
// =================================================================================

enum class AccessMethod
{
    Unchecked,
    Checked
};

template <typename Container, AccessMethod Method>
static void access_method(benchmark::State& state)
{
    const auto N = state.range(0);

    Container vec(N); // Pre-size the container
    // Fill it with dummy data
    for (size_t i = 0; i < N; ++i)
        vec[i] = static_cast<int>(i);

    // Randomize access pattern to prevent compiler optimizations
    std::vector<size_t> random_indices(N);
    std::iota(random_indices.begin(), random_indices.end(), 0);
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(random_indices.begin(), random_indices.end(), g);

    for (auto _ : state)
    {
        long long sum = 0;
        for (size_t idx : random_indices)
        {
            if constexpr (Method == AccessMethod::Unchecked)
            {
                sum += vec[idx];
            }
            else
            {
                sum += vec.at(idx);
            }
        }
        benchmark::DoNotOptimize(sum);
    }

    state.SetComplexityN(N);
}

BENCHMARK_TEMPLATE(access_method, forge::vector<int>, AccessMethod::Unchecked)
    ->RangeMultiplier(2)
    ->Range(1024, 32768)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

BENCHMARK_TEMPLATE(access_method, std::vector<int>, AccessMethod::Unchecked)
    ->RangeMultiplier(2)
    ->Range(1024, 32768)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

BENCHMARK_TEMPLATE(access_method, forge::vector<int>, AccessMethod::Checked)
    ->RangeMultiplier(2)
    ->Range(1024, 32768)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

BENCHMARK_TEMPLATE(access_method, std::vector<int>, AccessMethod::Checked)
    ->RangeMultiplier(2)
    ->Range(1024, 32768)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);
