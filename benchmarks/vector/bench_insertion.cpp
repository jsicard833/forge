#include <benchmark/benchmark.h>

#include <forge/vector.hpp>

#include "bench_utils.hpp"

#include <vector>

// =================================================================================
// Push Back
// =================================================================================

template <typename T> struct TrackingAlloc
{
    using value_type = T;

    static inline size_t total_allocated = 0;
    static inline size_t peak_allocated = 0;

    TrackingAlloc() noexcept = default;
    template <typename U> TrackingAlloc(const TrackingAlloc<U>&) noexcept {}

    T* allocate(size_t n)
    {
        size_t bytes = n * sizeof(T);
        total_allocated += bytes;
        if (total_allocated > peak_allocated)
        {
            peak_allocated = total_allocated;
        }
        return static_cast<T*>(::operator new(bytes));
    }

    void deallocate(T* p, size_t n) noexcept
    {
        total_allocated -= (n * sizeof(T));
        ::operator delete(p);
    }

    bool operator==(const TrackingAlloc&) const noexcept
    {
        return true;
    }
    bool operator!=(const TrackingAlloc&) const noexcept
    {
        return false;
    }
};

template <typename Container> static void push_back_growth(benchmark::State& state)
{
    const auto N = state.range(0);

    for (auto _ : state)
    {
        state.PauseTiming();
        Container vec;
        state.ResumeTiming();

        for (int i = 0; i < N; ++i)
        {
            vec.push_back(typename Container::value_type(i));
        }
        benchmark::DoNotOptimize(vec.data());
        benchmark::ClobberMemory();
    }

    state.SetComplexityN(N);
}

BENCHMARK(push_back_growth<forge::vector<int>>)
    ->DenseRange(0, 1024, 64)
    ->RangeMultiplier(2)
    ->Range(2048, 1 << 20)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

BENCHMARK(push_back_growth<std::vector<int>>)
    ->DenseRange(0, 1024, 64)
    ->RangeMultiplier(2)
    ->Range(2048, 1 << 20)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

BENCHMARK(push_back_growth<forge::vector<BigObject>>)
    ->DenseRange(0, 1024, 64)
    ->RangeMultiplier(2)
    ->Range(2048, 1 << 20)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

BENCHMARK(push_back_growth<std::vector<BigObject>>)
    ->DenseRange(0, 1024, 64)
    ->RangeMultiplier(2)
    ->Range(2048, 1 << 20)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

template <typename Container> static void push_back_reserve(benchmark::State& state)
{
    const auto N = state.range(0);

    for (auto _ : state)
    {
        state.PauseTiming();
        Container vec;
        vec.reserve(N); // Reserve to avoid measuring reallocation during insertion
        state.ResumeTiming();

        for (int i = 0; i < N; ++i)
        {
            vec.push_back(typename Container::value_type(i));
        }
        benchmark::DoNotOptimize(vec.data());
        benchmark::ClobberMemory();
    }

    state.SetComplexityN(N);
}

BENCHMARK(push_back_reserve<forge::vector<int>>)
    ->DenseRange(0, 1024, 64)
    ->RangeMultiplier(2)
    ->Range(2048, 1 << 20)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

BENCHMARK(push_back_reserve<std::vector<int>>)
    ->DenseRange(0, 1024, 64)
    ->RangeMultiplier(2)
    ->Range(2048, 1 << 20)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

BENCHMARK(push_back_reserve<forge::vector<BigObject>>)
    ->DenseRange(0, 1024, 64)
    ->RangeMultiplier(2)
    ->Range(2048, 1 << 20)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

BENCHMARK(push_back_reserve<std::vector<BigObject>>)
    ->DenseRange(0, 1024, 64)
    ->RangeMultiplier(2)
    ->Range(2048, 1 << 20)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

template <typename Container, typename Allocator>
static void push_back_memory(benchmark::State& state)
{
    const auto N = state.range(0);

    for (auto _ : state)
    {
        state.PauseTiming();
        Allocator::total_allocated = 0;
        Allocator::peak_allocated = 0;
        state.ResumeTiming();

        {
            Container vec;
            for (int i = 0; i < N; ++i)
            {
                vec.push_back(i);
            }

            state.PauseTiming();
            size_t final_capacity_bytes = vec.capacity() * sizeof(typename Container::value_type);
            size_t actual_used_bytes = N * sizeof(typename Container::value_type);
            size_t peak = Allocator::peak_allocated;

            state.counters["PeakBytes"] = benchmark::Counter(peak, benchmark::Counter::kDefaults);
            state.counters["WastedBytes"] = benchmark::Counter(
                final_capacity_bytes - actual_used_bytes, benchmark::Counter::kDefaults);
            state.counters["Efficiency%"] = benchmark::Counter(
                (static_cast<double>(actual_used_bytes) / final_capacity_bytes) * 100.0,
                benchmark::Counter::kDefaults);
            state.ResumeTiming();
        }
    }
}

BENCHMARK(push_back_memory<forge::vector<int, TrackingAlloc<int>>, TrackingAlloc<int>>)
    // === Powers of 2 ===
    ->Arg(1024)
    ->Arg(65536)
    ->Arg(1048576)

    // === Just over a power of 2 ===
    ->Arg(1025)
    ->Arg(65537)
    ->Arg(1048577)

    // === Powers of 1.5 ===
    ->Arg(1066)
    ->Arg(61447)
    ->Arg(1049869)

    // == Just over a power of 1.5 ===
    ->Arg(1067)
    ->Arg(61448)
    ->Arg(1049870)

    // === Halfway between 2x boundaries ===
    ->Arg(1536)
    ->Arg(6144)
    ->Arg(98304)

    // === Round "real world" sizes ===
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000)
    ->Arg(1000000)
    ->Arg(10000000);

BENCHMARK(push_back_memory<std::vector<int, TrackingAlloc<int>>, TrackingAlloc<int>>)
    // === Powers of 2 ===
    ->Arg(1024)
    ->Arg(65536)
    ->Arg(1048576)

    // === Just over a power of 2 ===
    ->Arg(1025)
    ->Arg(65537)
    ->Arg(1048577)

    // === Powers of 1.5 ===
    ->Arg(1066)
    ->Arg(61447)
    ->Arg(1049869)

    // == Just over a power of 1.5 ===
    ->Arg(1067)
    ->Arg(61448)
    ->Arg(1049870)

    // === Halfway between 2x boundaries ===
    ->Arg(1536)
    ->Arg(6144)
    ->Arg(98304)

    // === Round "real world" sizes ===
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000)
    ->Arg(1000000)
    ->Arg(10000000);

// =================================================================================
// Emplace Back
// =================================================================================

struct MockTrackedObject
{
    int x;
    double y;

    // Instrumentation counters
    inline static size_t constructor_calls = 0;
    inline static size_t copy_calls = 0;
    inline static size_t move_calls = 0;

    // Reset helper for the benchmark setup
    static void reset_counters()
    {
        constructor_calls = 0;
        copy_calls = 0;
        move_calls = 0;
    }

    // 1. Standard Constructor
    explicit MockTrackedObject(int a, double b) : x(a), y(b)
    {
        ++constructor_calls;
    }

    // 2. Copy Constructor
    MockTrackedObject(const MockTrackedObject& other) : x(other.x), y(other.y)
    {
        ++copy_calls;
    }

    // 3. Move Constructor
    MockTrackedObject(MockTrackedObject&& other) noexcept : x(other.x), y(other.y)
    {
        ++move_calls;
    }
};

enum class EmplaceMode
{
    Emplace,
    Push
};

template <typename Container, EmplaceMode Mode>
static void emplace_back_vs_push_back(benchmark::State& state)
{
    const auto N = state.range(0);

    for (auto _ : state)
    {
        state.PauseTiming();
        MockTrackedObject::reset_counters();
        Container vec;
        vec.reserve(N); // Reserve to avoid measuring reallocation during insertion
        state.ResumeTiming();

        for (int i = 0; i < N; ++i)
        {
            if constexpr (Mode == EmplaceMode::Emplace)
            {
                vec.emplace_back(42, 3.14); // Emplace with constructor arguments
            }
            else
            {
                vec.push_back(MockTrackedObject(42, 3.14)); // Push with a temporary object
            }
        }

        benchmark::DoNotOptimize(vec.data());
        benchmark::ClobberMemory();
    }

    state.counters["Total_Moves"] = MockTrackedObject::move_calls;
    state.counters["Total_Constructors"] = MockTrackedObject::constructor_calls;

    state.SetComplexityN(N);
}

BENCHMARK(emplace_back_vs_push_back<forge::vector<MockTrackedObject>, EmplaceMode::Push>)
    ->Range(128, 4096)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

BENCHMARK(emplace_back_vs_push_back<forge::vector<MockTrackedObject>, EmplaceMode::Emplace>)
    ->Range(128, 4096)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

BENCHMARK(emplace_back_vs_push_back<std::vector<MockTrackedObject>, EmplaceMode::Push>)
    ->Range(128, 4096)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

BENCHMARK(emplace_back_vs_push_back<std::vector<MockTrackedObject>, EmplaceMode::Emplace>)
    ->Range(128, 4096)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

// =================================================================================
// Insertions at Arbitrary Positions
// =================================================================================

template <typename Container> static void insert_front(benchmark::State& state)
{
    const auto N = state.range(0);

    for (auto _ : state)
    {
        // Pause timing to isolate the insertion cost from construction
        state.PauseTiming();
        Container vec;
        vec.reserve(N + 1); // Reserve to avoid measuring reallocation during insertion
        for (int i = 0; i < N; ++i)
        {
            vec.push_back(typename Container::value_type(0));
        }
        state.ResumeTiming();

        vec.insert(vec.begin(), typename Container::value_type(42));

        benchmark::DoNotOptimize(vec.data());
        benchmark::ClobberMemory();
    }

    state.SetComplexityN(N);
}

BENCHMARK(insert_front<forge::vector<int>>)
    ->RangeMultiplier(4)
    ->Range(128, 65536)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

BENCHMARK(insert_front<std::vector<int>>)
    ->RangeMultiplier(4)
    ->Range(128, 65536)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

BENCHMARK(insert_front<forge::vector<BigObject>>)
    ->RangeMultiplier(4)
    ->Range(128, 65536)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

BENCHMARK(insert_front<std::vector<BigObject>>)
    ->RangeMultiplier(4)
    ->Range(128, 65536)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

template <typename Container> static void insert_middle(benchmark::State& state)
{
    const auto N = state.range(0);

    for (auto _ : state)
    {
        // Pause timing to isolate the insertion cost from construction
        state.PauseTiming();
        Container vec;
        vec.reserve(N + 1); // Reserve to avoid measuring reallocation during insertion
        for (int i = 0; i < N; ++i)
        {
            vec.push_back(typename Container::value_type(0));
        }

        auto mid_it = vec.begin() + (vec.size() / 2);
        state.ResumeTiming();

        vec.insert(mid_it, typename Container::value_type(42));

        benchmark::DoNotOptimize(vec.data());
        benchmark::ClobberMemory();
    }

    state.SetComplexityN(N);
}

BENCHMARK(insert_middle<forge::vector<int>>)
    ->RangeMultiplier(4)
    ->Range(128, 65536)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

BENCHMARK(insert_middle<std::vector<int>>)
    ->RangeMultiplier(4)
    ->Range(128, 65536)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

BENCHMARK(insert_middle<forge::vector<BigObject>>)
    ->RangeMultiplier(4)
    ->Range(128, 65536)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);

BENCHMARK(insert_middle<std::vector<BigObject>>)
    ->RangeMultiplier(4)
    ->Range(128, 65536)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kNanosecond);
