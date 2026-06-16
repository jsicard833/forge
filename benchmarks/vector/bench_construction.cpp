#include <benchmark/benchmark.h>

#include <forge/vector.hpp>

#include <string>
#include <vector>

// =================================================================================
// Default Construction
// =================================================================================

template <typename Container> static void default_construction(benchmark::State& state)
{
    for (auto _ : state)
    {
        Container vec;

        benchmark::DoNotOptimize(vec.data());
    }
}

BENCHMARK(default_construction<forge::vector<int>>)->Unit(benchmark::kNanosecond);

BENCHMARK(default_construction<std::vector<int>>)->Unit(benchmark::kNanosecond);

// =================================================================================
// Size-Based Construction
// =================================================================================

template <typename Container> static void size_construction(benchmark::State& state)
{
    const auto size = state.range(0);

    for (auto _ : state)
    {
        Container vec(size);

        benchmark::DoNotOptimize(vec.data());
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(size);

    state.SetComplexityN(size);
}

// Primitive Type (int)
BENCHMARK(size_construction<forge::vector<int>>)
    ->RangeMultiplier(4)
    ->Range(64, 4096)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

BENCHMARK(size_construction<std::vector<int>>)
    ->RangeMultiplier(4)
    ->Range(64, 4096)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

// Complex Type (std::string)
BENCHMARK(size_construction<forge::vector<std::string>>)
    ->RangeMultiplier(4)
    ->Range(64, 4096)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

BENCHMARK(size_construction<std::vector<std::string>>)
    ->RangeMultiplier(4)
    ->Range(64, 4096)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

// =================================================================================
// Copy vs Move Construction
// =================================================================================

inline thread_local size_t g_heap_allocations = 0;

struct TrackedHeavyObject
{
    int* data;

    TrackedHeavyObject() : data(new int(0))
    {
        g_heap_allocations++;
    }

    // Copy Constructor (Deep Copy)
    TrackedHeavyObject(const TrackedHeavyObject& other) : data(new int(*other.data))
    {
        g_heap_allocations++;
    }

    // Move Constructor (Shallow Copy / Pointer Swap)
    TrackedHeavyObject(TrackedHeavyObject&& other) noexcept : data(other.data)
    {
        other.data = nullptr; // No new allocation
    }

    ~TrackedHeavyObject()
    {
        delete data;
    }
};

enum class ConstructionMode
{
    Copy,
    Move
};

template <typename Container, ConstructionMode Mode>
static void copy_move_construction(benchmark::State& state)
{
    const auto size = state.range(0);

    for (auto _ : state)
    {
        state.PauseTiming();    // Pause timing to set up source container
        Container source(size); // Create source container with 'size' elements
        g_heap_allocations = 0; // Reset allocation count before each iteration
        state.ResumeTiming();   // Resume timing for the construction being tested

        if constexpr (Mode == ConstructionMode::Copy)
        {
            Container destination(source); // Copy construction
            benchmark::DoNotOptimize(destination.data());
            benchmark::ClobberMemory();
        }
        else
        {
            // Isolate O(1) move construction from O(N) destruction
            {
                Container destination(std::move(source)); // Move construction
                benchmark::DoNotOptimize(destination.data());
                benchmark::ClobberMemory();

                state.PauseTiming();
            }

            state.ResumeTiming();
        }
    }

    state.SetItemsProcessed(size);
    state.SetComplexityN(size);

    state.counters["Allocations/Op"] = benchmark::Counter(static_cast<double>(g_heap_allocations),
                                                          benchmark::Counter::kAvgThreads);
}

// Copy Construction
BENCHMARK(copy_move_construction<forge::vector<TrackedHeavyObject>, ConstructionMode::Copy>)
    ->RangeMultiplier(4)
    ->Range(64, 4096)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

BENCHMARK(copy_move_construction<std::vector<TrackedHeavyObject>, ConstructionMode::Copy>)
    ->RangeMultiplier(4)
    ->Range(64, 4096)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

// Move Construction
BENCHMARK(copy_move_construction<forge::vector<TrackedHeavyObject>, ConstructionMode::Move>)
    ->RangeMultiplier(4)
    ->Range(64, 4096)
    ->Complexity(benchmark::o1)
    ->Iterations(50000)
    ->Unit(benchmark::kNanosecond);

BENCHMARK(copy_move_construction<std::vector<TrackedHeavyObject>, ConstructionMode::Move>)
    ->RangeMultiplier(4)
    ->Range(64, 4096)
    ->Complexity(benchmark::o1)
    ->Iterations(50000)
    ->Unit(benchmark::kNanosecond);