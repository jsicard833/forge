#include <algorithm>
#include <cstddef>
#include <iterator>
#include <list>
#include <memory>
#include <new>
#include <random>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_all.hpp>

#include <forge/vector.hpp>

// ==========================================================
// Helpers and Structs
// ==========================================================

template <typename T> struct IdAllocator
{
    using value_type = T;

    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_copy_assignment = std::true_type;
    using propagate_on_container_swap = std::true_type;

    int id; // Custom identifier to track which allocator instance is being used

    IdAllocator(int id) : id(id) {}

    template <typename U> IdAllocator(const IdAllocator<U>& other) : id(other.id) {}
    T* allocate(std::size_t n)
    {
        return std::allocator<T>{}.allocate(n);
    }
    void deallocate(T* p, std::size_t n)
    {
        std::allocator<T>{}.deallocate(p, n);
    }

    IdAllocator select_on_container_copy_construction() const
    {
        return IdAllocator(
            id + 1); // For testing purposes, we return a new allocator with a different ID to
                     // verify that the copy constructor is using the correct allocator behavior
    }

    bool operator==(const IdAllocator& other) const
    {
        return id == other.id;
    }
};

struct ThrowsAfterNConstructions
{
    static inline int count = 0;
    static inline int throw_at = -1;

    ThrowsAfterNConstructions()
    {
        if (count == throw_at)
        {
            throw std::runtime_error("Constructor threw an exception");
        }
        ++count;
    }

    ThrowsAfterNConstructions(int value)
        : ThrowsAfterNConstructions() {} // Delegating constructor to reuse the counting logic

    ThrowsAfterNConstructions(const ThrowsAfterNConstructions&)
        : ThrowsAfterNConstructions() {} // Copy constructor

    ~ThrowsAfterNConstructions()
    {
        count--;
    }

    static void reset(int limit = -1)
    {
        count = 0;
        throw_at = limit;
    }
};

template <typename T> struct ToggleAllocator : std::allocator<T>
{
    static inline bool should_fail = false;

    template <typename U> struct rebind
    {
        typedef ToggleAllocator<U> other;
    };

    T* allocate(std::size_t n)
    {
        if (should_fail)
        {
            throw std::bad_alloc(); // Always throw an exception when trying to allocate memory
        }
        return std::allocator<T>{}.allocate(n);
    }

    void deallocate(T* p, std::size_t n)
    {
        std::allocator<T>{}.deallocate(p, n);
    }
};

struct Tracked
{
    static inline int default_constructed = 0;
    static inline int copy_constructed = 0;
    static inline int move_constructed = 0;

    static void reset()
    {
        default_constructed = 0;
        copy_constructed = 0;
        move_constructed = 0;
    }

    Tracked()
    {
        default_constructed++;
    }
    Tracked(const Tracked&)
    {
        copy_constructed++;
    }
    Tracked(Tracked&&) noexcept
    {
        move_constructed++;
    }
};

template <typename T> struct forge::detail::vector_tests_accessor
{
    static void reallocate(forge::vector<int>& vec)
    {
        vec.reallocate();
    }
};

struct MoveOnly
{
    MoveOnly() = default;
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&&) = default;
    MoveOnly& operator=(const MoveOnly&) = delete;
    MoveOnly& operator=(MoveOnly&&) = default;
    int value{0};
};

struct ThrowingMove
{
    ThrowingMove() = default;
    ThrowingMove& operator=(ThrowingMove&&) noexcept(false)
    {
        return *this;
    }
};

template <typename T, bool PropagateMove = false, bool AlwaysEqual = false>
struct CustomTestAllocator
{
    using value_type = T;
    using propagate_on_container_move_assignment = std::bool_constant<PropagateMove>;
    using is_always_equal = std::bool_constant<AlwaysEqual>;

    int id = 0;

    CustomTestAllocator() = default;
    explicit CustomTestAllocator(int instance_id) : id(instance_id) {}

    template <typename U> struct rebind
    {
        using other = CustomTestAllocator<U, PropagateMove, AlwaysEqual>;
    };

    T* allocate(std::size_t n)
    {
        return std::allocator<T>{}.allocate(n);
    }
    void deallocate(T* p, std::size_t n)
    {
        std::allocator<T>{}.deallocate(p, n);
    }

    bool operator==(const CustomTestAllocator& other) const
    {
        if constexpr (AlwaysEqual)
            return true;
        return id == other.id;
    }
};

// A strict single-pass input iterator wrapping an integer pointer
struct MinimalInputIterator
{
    using iterator_category = std::input_iterator_tag;
    using value_type = int;
    using difference_type = std::ptrdiff_t;
    using pointer = const int*;
    using reference = const int&;

    const int* ptr = nullptr;

    constexpr auto operator*() const noexcept -> reference
    {
        return *ptr;
    }
    constexpr auto operator->() const noexcept -> pointer
    {
        return ptr;
    }

    constexpr auto operator++() noexcept -> MinimalInputIterator&
    {
        ++ptr;
        return *this;
    }
    constexpr auto operator++(int) noexcept -> MinimalInputIterator
    {
        MinimalInputIterator tmp = *this;
        ++ptr;
        return tmp;
    }
    constexpr friend auto operator==(const MinimalInputIterator& a,
                                     const MinimalInputIterator& b) noexcept -> bool
    {
        return a.ptr == b.ptr;
    }
};

// ==========================================================
// Tests
// ==========================================================

TEST_CASE("Default vector construction", "[vector][constructors]")
{
    forge::vector<int> vec;

    SECTION("initializes size, capacity, and data pointer correctly")
    {
        CHECK(vec.size() == 0);
        CHECK(vec.capacity() == 0);
        CHECK(vec.data() == nullptr);
    }

    SECTION("honors the noexcept contract")
    {
        STATIC_REQUIRE(noexcept(forge::vector<int>()));
    }
}

TEST_CASE("Vector construction with a given allocator", "[vector][constructors]")
{
    IdAllocator<int> id_alloc(42);
    forge::vector<int, IdAllocator<int>> vec(id_alloc);

    SECTION("initializes size, capacity, and data pointer correctly")
    {
        CHECK(vec.size() == 0);
        CHECK(vec.capacity() == 0);
        CHECK(vec.data() == nullptr);
    }

    SECTION("uses the provided allocator for memory management")
    {
        REQUIRE(vec.get_allocator() == id_alloc);
        CHECK(vec.get_allocator().id == 42); // The allocator's ID should match the one we provided
    }

    SECTION("honors the noexcept contract")
    {
        STATIC_REQUIRE(noexcept(forge::vector<int, IdAllocator<int>>(id_alloc)));
    }
}

TEST_CASE("Vector construction with size", "[vector][constructors]")
{
    SECTION("creates a vector of the specified size for a non-zero size")
    {
        forge::vector<int> vec(5);

        CHECK(vec.size() == 5);
        CHECK(vec.capacity() == 5);
        CHECK(vec.data() != nullptr);
    }

    SECTION("creates an empty vector for a zero size")
    {
        forge::vector<int> vec(0);

        CHECK(vec.size() == 0);
        CHECK(vec.capacity() == 0);
        CHECK(vec.data() == nullptr);
    }

    SECTION("throws std::length_error if count exceeds max_size()")
    {
        forge::vector<int> vec;
        REQUIRE_THROWS_AS(forge::vector<int>(vec.max_size() + 1), std::length_error);
    }

    SECTION("default-constructs elements")
    {
        Tracked::reset();
        forge::vector<Tracked> vec(3);

        CHECK(vec.size() == 3);
        CHECK(vec.capacity() == 3);
        CHECK(vec.data() != nullptr);
        CHECK(Tracked::default_constructed == 3); // All elements should be default-constructed
    }

    SECTION("handles allocation failures")
    {
        ToggleAllocator<int>::should_fail =
            true; // Enable allocation failure for the ToggleAllocator
        REQUIRE_THROWS_AS((forge::vector<int, ToggleAllocator<int>>(5)), std::bad_alloc);
    }

    SECTION("honors the exception safety guarantees")
    {
        ThrowsAfterNConstructions::reset(
            5); // Set the constructor to throw after 5 default constructions

        REQUIRE_THROWS_AS((forge::vector<ThrowsAfterNConstructions>(100)), std::runtime_error);
        CHECK(ThrowsAfterNConstructions::count ==
              0); // All constructed elements should have been destroyed
    }
}

TEST_CASE("Vector construction with size and value", "[vector][constructors]")
{
    SECTION("creates a vector with the specified size and initializes all elements to the provided "
            "value for a non-zero size")
    {
        forge::vector<int> vec(5, 42);

        CHECK(vec.size() == 5);
        CHECK(vec.capacity() == 5);
        REQUIRE(vec.data() != nullptr);
        for (std::size_t i = 0; i < vec.size(); ++i)
        {
            CHECK(vec[i] == 42); // Elements should be initialized to the provided value
        }
    }

    SECTION("creates an empty vector when size is zero, regardless of the provided value")
    {
        forge::vector<int> vec(0, 42);

        CHECK(vec.size() == 0);
        CHECK(vec.capacity() == 0);
        CHECK(vec.data() == nullptr);
    }

    SECTION("throws std::length_error if count exceeds max_size()")
    {
        forge::vector<int> vec;
        REQUIRE_THROWS_AS(forge::vector<int>(vec.max_size() + 1, 42), std::length_error);
    }

    SECTION("handles allocation failures")
    {
        ToggleAllocator<int>::should_fail =
            true; // Enable allocation failure for the ToggleAllocator
        REQUIRE_THROWS_AS((forge::vector<int, ToggleAllocator<int>>(5, 42)), std::bad_alloc);
    }

    SECTION("honors the exception safety guarantees")
    {
        ThrowsAfterNConstructions::reset(5); // Set the constructor to throw after 5 constructions

        REQUIRE_THROWS_AS((forge::vector<ThrowsAfterNConstructions>(100, 42)), std::runtime_error);
        CHECK(ThrowsAfterNConstructions::count ==
              0); // All constructed elements should have been destroyed
    }
}

TEST_CASE("Vector construction from range of iterators", "[vector][constructors]")
{
    SECTION("creates a vector with the elements from the specified range of iterators")
    {
        std::vector<int> source{1, 2, 3, 4, 5};
        forge::vector<int> vec(source.begin(), source.end());

        REQUIRE(vec.size() == source.size());
        CHECK(vec.capacity() == source.size());
        REQUIRE(vec.data() != nullptr);
        for (std::size_t i = 0; i < vec.size(); ++i)
        {
            CHECK(vec[i] == source[i]); // Elements should match the source vector
        }
    }

    SECTION("creates a vector with the elements from the specified range of non random-access "
            "input iterators")
    {
        std::list<int> source{1, 2, 3, 4, 5};
        forge::vector<int> vec(source.begin(), source.end());

        REQUIRE(vec.size() == source.size());
        CHECK(vec.capacity() == source.size());
        REQUIRE(vec.data() != nullptr);
        auto it = source.begin();
        for (std::size_t i = 0; i < vec.size(); ++i, ++it)
        {
            CHECK(vec[i] == *it); // Elements should match the source list
        }
    }

    SECTION("creates a vector with the elements from the specified range of iterators when the "
            "type is move-only")
    {
        std::vector<MoveOnly> source(5);
        forge::vector<MoveOnly> vec(std::make_move_iterator(source.begin()),
                                    std::make_move_iterator(source.end()));

        REQUIRE(vec.size() == source.size());
        CHECK(vec.capacity() == source.size());
        REQUIRE(vec.data() != nullptr);
        for (std::size_t i = 0; i < vec.size(); ++i)
        {
            CHECK(vec[i].value ==
                  0); // Elements should be default-initialized in the move-only case
        }
    }

    SECTION("creates an empty vector when the specified range is empty")
    {
        std::vector<int> source;
        forge::vector<int> vec(source.begin(), source.end());

        CHECK(vec.size() == 0);
        CHECK(vec.capacity() == 0);
        CHECK(vec.data() == nullptr);
    }

    SECTION("works for const iterators")
    {
        std::vector<int> source{1, 2, 3, 4, 5};
        forge::vector<int> vec(source.cbegin(), source.cend());

        REQUIRE(vec.size() == source.size());
        CHECK(vec.capacity() == source.size());
        REQUIRE(vec.data() != nullptr);
        for (std::size_t i = 0; i < vec.size(); ++i)
        {
            CHECK(vec[i] == source[i]); // Elements should match the source vector
        }
    }

    SECTION("handles allocation failures")
    {
        ToggleAllocator<int>::should_fail =
            true; // Enable allocation failure for the ToggleAllocator
        std::vector<int> source(5, 42);
        REQUIRE_THROWS_AS((forge::vector<int, ToggleAllocator<int>>(source.begin(), source.end())),
                          std::bad_alloc);
    }

    SECTION("honors the exception safety guarantees")
    {
        ThrowsAfterNConstructions::reset(
            -1); // Disable thorowing so we can create the source vector
        std::vector<ThrowsAfterNConstructions> source(
            100);                            // Create a source vector with 100 elements
        ThrowsAfterNConstructions::reset(5); // Set the constructor to throw after 5 constructions

        REQUIRE_THROWS_AS((forge::vector<ThrowsAfterNConstructions>(source.begin(), source.end())),
                          std::runtime_error);
        CHECK(ThrowsAfterNConstructions::count ==
              0); // All constructed elements should have been destroyed
    }

    SECTION("fast path: works for trivially copyable types with contiguous iterators")
    {
        // Define a trivially copyable custom type to prove it's not just primitives
        struct Point
        {
            int x;
            int y;
        };
        std::array<Point, 3> source{{{1, 2}, {3, 4}, {5, 6}}};

        // std::array::iterator is a std::contiguous_iterator, triggering the memmove path
        forge::vector<Point> vec(source.begin(), source.end());

        REQUIRE(vec.size() == source.size());
        CHECK(vec.capacity() == source.size());
        REQUIRE(vec.data() != nullptr);

        CHECK(vec[0].x == 1);
        CHECK(vec[2].y == 6);
    }

    SECTION("safe path: handles pure input iterators without draining them via std::distance")
    {
        // An istringstream iterator is a pure input iterator (not a forward iterator)
        std::istringstream stream("10 20 30 40 50");
        std::istream_iterator<int> first(stream);
        std::istream_iterator<int> last;

        // This tests that our implementation loops dynamically using emplace_back style
        // allocations without failing or crashing on std::distance.
        forge::vector<int> vec(first, last);

        REQUIRE(vec.size() == 5);
        CHECK(vec.capacity() >= 5);

        CHECK(vec[0] == 10);
        CHECK(vec[4] == 50);
    }

    SECTION("hybrid path: handles trivially copyable types with non-contiguous forward iterators")
    {
        // std::list::iterator is a bidirectional iterator, meaning it is a forward iterator,
        // but NOT a contiguous iterator. It must use the single-allocation chunk size path,
        // but fall back to standard traits::construct loops instead of raw memmove.
        std::list<int> source{100, 200, 300};
        forge::vector<int> vec(source.begin(), source.end());

        REQUIRE(vec.size() == 3);
        CHECK(vec.capacity() == 3); // Pre-computed via std::distance precisely
        CHECK(vec[0] == 100);
        CHECK(vec[2] == 300);
    }
}

TEST_CASE("Vector construction from initializer list", "[vector][constructors]")
{
    SECTION("creates a vector with the elements from the specified initializer list")
    {
        forge::vector<int> vec{1, 2, 3, 4, 5};

        REQUIRE(vec.size() == 5);
        CHECK(vec.capacity() == 5);
        REQUIRE(vec.data() != nullptr);
        for (std::size_t i = 0; i < vec.size(); ++i)
        {
            CHECK(vec[i] == static_cast<int>(i + 1)); // Elements should match the initializer list
        }
    }

    SECTION("creates an empty vector when the specified initializer list is empty")
    {
        forge::vector<int> vec{};

        CHECK(vec.size() == 0);
        CHECK(vec.capacity() == 0);
        CHECK(vec.data() == nullptr);
    }

    SECTION("handles allocation failures")
    {
        ToggleAllocator<int>::should_fail =
            true; // Enable allocation failure for the ToggleAllocator
        REQUIRE_THROWS_AS((forge::vector<int, ToggleAllocator<int>>{1, 2, 3}), std::bad_alloc);
    }

    SECTION("honors the exception safety guarantees")
    {
        ThrowsAfterNConstructions::reset(5); // Set the constructor to throw after 5 constructions

        REQUIRE_THROWS_AS((forge::vector<ThrowsAfterNConstructions>{ThrowsAfterNConstructions(),
                                                                    ThrowsAfterNConstructions(),
                                                                    ThrowsAfterNConstructions()}),
                          std::runtime_error);
        CHECK(ThrowsAfterNConstructions::count ==
              0); // All constructed elements should have been destroyed
    }
}

TEST_CASE("Vector copy construction", "[vector][constructors]")
{
    SECTION("creates a new vector that is a copy of the original vector")
    {
        forge::vector<int> original{1, 2, 3, 4, 5};
        forge::vector<int> copy(original);

        REQUIRE(copy.size() == original.size());
        CHECK(copy.capacity() == original.capacity());
        REQUIRE(copy.data() != nullptr);
        for (std::size_t i = 0; i < copy.size(); ++i)
        {
            CHECK(copy[i] == original[i]); // Elements should match the original vector
        }
    }

    SECTION("creates a new vector that is independent of the original vector")
    {
        forge::vector<int> original{1, 2, 3, 4, 5};
        forge::vector<int> copy(original);

        // Modify the original vector and check that the copy is unaffected
        original[0] = 42;

        CHECK(copy[0] == 1); // The copy should still have the original value
    }

    SECTION("creates an empty vector when the original vector is empty")
    {
        forge::vector<int> original;
        forge::vector<int> copy(original);

        CHECK(copy.size() == 0);
        CHECK(copy.capacity() == 0);
        CHECK(copy.data() == nullptr);
    }

    SECTION("works for const vectors")
    {
        const forge::vector<int> original{1, 2, 3, 4, 5};
        forge::vector<int> copy(original);

        REQUIRE(copy.size() == original.size());
        CHECK(copy.capacity() == original.capacity());
        REQUIRE(copy.data() != nullptr);
        for (std::size_t i = 0; i < copy.size(); ++i)
        {
            CHECK(copy[i] == original[i]); // Elements should match the original vector
        }
    }

    SECTION("copies the allocator according to the allocator's copy construction behavior")
    {
        IdAllocator<int> id_alloc(42);
        forge::vector<int, IdAllocator<int>> original(id_alloc);
        forge::vector<int, IdAllocator<int>> copy(original);

        CHECK(copy.get_allocator().id ==
              43); // The copy constructor should have used select_on_container_copy_construction to
                   // get a new allocator with a different ID
    }

    SECTION("honors the exception safety guarantees")
    {
        ThrowsAfterNConstructions::reset(
            -1); // Disable throwing so we can create the original vector
        forge::vector<ThrowsAfterNConstructions> original(
            100);                            // Create an original vector with 100 elements
        ThrowsAfterNConstructions::reset(5); // Set the constructor to throw after 5 constructions

        REQUIRE_THROWS_AS((forge::vector<ThrowsAfterNConstructions>(original)), std::runtime_error);
        CHECK(ThrowsAfterNConstructions::count ==
              0); // All constructed elements should have been destroyed
    }
}

TEST_CASE("Vector move construction", "[vector][constructors]")
{
    SECTION("creates a new vector that takes ownership of the original vector's resources")
    {
        forge::vector<int> original{1, 2, 3, 4, 5};
        forge::vector<int> moved(std::move(original));

        REQUIRE(moved.size() == 5);
        CHECK(moved.capacity() == 5);
        REQUIRE(moved.data() != nullptr);
        for (std::size_t i = 0; i < moved.size(); ++i)
        {
            CHECK(moved[i] == static_cast<int>(i + 1)); // Elements should match the original vector
        }
    }

    SECTION("leaves the original vector in a valid but empty state")
    {
        forge::vector<int> original{1, 2, 3, 4, 5};
        forge::vector<int> moved(std::move(original));

        CHECK(original.size() == 0);
        CHECK(original.capacity() == 0);
        CHECK(original.data() == nullptr);
    }

    SECTION("works for const vectors")
    {
        const forge::vector<int> original{1, 2, 3, 4, 5};
        forge::vector<int> moved(std::move(original));

        REQUIRE(moved.size() == 5);
        CHECK(moved.capacity() == 5);
        REQUIRE(moved.data() != nullptr);
        for (std::size_t i = 0; i < moved.size(); ++i)
        {
            CHECK(moved[i] == static_cast<int>(i + 1)); // Elements should match the original vector
        }
    }

    SECTION("moves the allocator")
    {
        IdAllocator<int> id_alloc(42);
        forge::vector<int, IdAllocator<int>> original(id_alloc);
        forge::vector<int, IdAllocator<int>> moved(std::move(original));

        CHECK(moved.get_allocator().id ==
              42); // The move constructor should have moved the allocator without changing its ID
    }

    SECTION("honors the noexcept contract")
    {
        STATIC_REQUIRE(noexcept(forge::vector<int>(std::move(std::declval<forge::vector<int>>()))));
    }
}

TEST_CASE("Vector destructor", "[vector][destructor]")
{
    SECTION("destroys all elements and deallocates memory")
    {
        ThrowsAfterNConstructions::reset(-1); // Disable throwing so we can create the vector
        {
            forge::vector<ThrowsAfterNConstructions> vec(5);
            CHECK(ThrowsAfterNConstructions::count ==
                  5); // All elements should be default-constructed
        }
        CHECK(ThrowsAfterNConstructions::count == 0); // All elements should have been destroyed
    }

    SECTION("handles self-destruction correctly")
    {
        forge::vector<int>* vec = new forge::vector<int>{1, 2, 3, 4, 5};
        delete vec; // The destructor should correctly clean up the vector's resources without any
                    // issues
    }
}

TEST_CASE("Vector copy assignment operator", "[vector][assignment]")
{
    SECTION("assigns one vector to another, copying all elements")
    {
        forge::vector<int> original{1, 2, 3, 4, 5};
        forge::vector<int> copy{6, 7, 8}; // Start with a different vector to ensure that the copy
                                          // assignment operator is actually copying the elements
        copy = original;

        REQUIRE(copy.size() == original.size());
        CHECK(copy.capacity() == original.size());
        REQUIRE(copy.data() != nullptr);
        for (std::size_t i = 0; i < copy.size(); ++i)
        {
            CHECK(copy[i] == original[i]); // Elements should match the original vector
        }
    }

    SECTION("copied elements are independent of the original vector")
    {
        forge::vector<int> original{1, 2, 3, 4, 5};
        forge::vector<int> copy;
        copy = original;

        // Modify the original vector and check that the copy is unaffected
        original[0] = 42;

        CHECK(copy[0] == 1); // The copy should still have the original value
    }

    SECTION("works for empty vectors")
    {
        forge::vector<int> original;
        forge::vector<int> copy;
        copy = original;

        CHECK(copy.size() == 0);
        CHECK(copy.capacity() == 0);
        CHECK(copy.data() == nullptr);
    }

    SECTION("works for const vectors")
    {
        const forge::vector<int> original{1, 2, 3, 4, 5};
        forge::vector<int> copy;
        copy = original;

        REQUIRE(copy.size() == original.size());
        CHECK(copy.capacity() == original.size());
        REQUIRE(copy.data() != nullptr);
        for (std::size_t i = 0; i < copy.size(); ++i)
        {
            CHECK(copy[i] == original[i]); // Elements should match the original vector
        }
    }

    SECTION("copies the allocator according to the allocator's copy assignment behavior")
    {
        IdAllocator<int> id_alloc(42);
        forge::vector<int, IdAllocator<int>> original(id_alloc);
        forge::vector<int, IdAllocator<int>> copy(id_alloc);
        copy = original;

        CHECK(
            copy.get_allocator().id ==
            43); // The copy assignment operator should have used
                 // select_on_container_copy_construction to get a new allocator with a different ID
    }

    SECTION("handles self-assignment correctly")
    {
        forge::vector<int> vec{1, 2, 3, 4, 5};
        vec = vec; // Self-assignment should not cause any issues
        REQUIRE(vec.size() == 5);
        CHECK(vec.capacity() == 5);
        REQUIRE(vec.data() != nullptr);
        for (std::size_t i = 0; i < vec.size(); ++i)
        {
            CHECK(vec[i] == static_cast<int>(
                                i + 1)); // Elements should remain unchanged after self-assignment
        }
    }

    SECTION("honors the exception safety guarantees")
    {
        ThrowsAfterNConstructions::reset(
            -1); // Disable throwing so we can create the original vector
        forge::vector<ThrowsAfterNConstructions> original(
            100);                            // Create an original vector with 100 elements
        ThrowsAfterNConstructions::reset(5); // Set the constructor to throw after 5 constructions

        forge::vector<ThrowsAfterNConstructions> copy;
        REQUIRE_THROWS_AS((copy = original), std::runtime_error);
        CHECK(ThrowsAfterNConstructions::count ==
              0); // All constructed elements should have been destroyed
    }
}

TEST_CASE("Vector move assignment operator", "[vector][assignment]")
{
    SECTION("assigns one vector to another, moving all elements")
    {
        forge::vector<int> original{1, 2, 3, 4, 5};
        forge::vector<int> moved{6, 7, 8}; // Start with a different vector to ensure that the move
                                           // assignment operator is actually moving the elements
        moved = std::move(original);

        REQUIRE(moved.size() == 5);
        CHECK(moved.capacity() == 5);
        REQUIRE(moved.data() != nullptr);
        for (std::size_t i = 0; i < moved.size(); ++i)
        {
            CHECK(moved[i] == static_cast<int>(i + 1)); // Elements should match the original vector
        }
    }

    SECTION("works for empty vectors")
    {
        forge::vector<int> original;
        forge::vector<int> moved;
        moved = std::move(original);

        CHECK(moved.size() == 0);
        CHECK(moved.capacity() == 0);
        CHECK(moved.data() == nullptr);
    }

    SECTION("handles self-assignment correctly")
    {
        forge::vector<int> vec{1, 2, 3, 4, 5};
        vec = std::move(vec); // Self-assignment should not cause any issues
        REQUIRE(vec.size() == 5);
        CHECK(vec.capacity() == 5);
        REQUIRE(vec.data() != nullptr);
        for (std::size_t i = 0; i < vec.size(); ++i)
        {
            CHECK(vec[i] == static_cast<int>(
                                i + 1)); // Elements should remain unchanged after self-assignment
        }
    }

    SECTION("moves the allocator")
    {
        IdAllocator<int> id_alloc(42);
        IdAllocator<int> id_alloc2(
            43); // Create a second allocator with a different ID to ensure that the move assignment
                 // operator is actually moving the allocator
        forge::vector<int, IdAllocator<int>> original(id_alloc);
        forge::vector<int, IdAllocator<int>> moved(id_alloc2);
        moved = std::move(original);

        CHECK(moved.get_allocator().id == 42); // The move assignment operator should have moved the
                                               // allocator without changing its ID
    }

    SECTION("leaves the original vector in a valid but empty state")
    {
        forge::vector<int> original{1, 2, 3, 4, 5};
        forge::vector<int> moved;
        moved = std::move(original);

        CHECK(original.size() == 0);
        CHECK(original.capacity() == 0);
        CHECK(original.data() == nullptr);
    }

    SECTION("Path 1: POCMA is true - steals buffer and explicitly overwrites allocator")
    {
        // Set PropagateMove = true
        using AllocType = CustomTestAllocator<int, true, false>;

        forge::vector<int, AllocType> original({1, 2, 3}, AllocType(100));
        forge::vector<int, AllocType> moved({4, 5}, AllocType(200));

        int* original_ptr = original.data();

        // Perform the move assignment
        moved = std::move(original);

        // Allocator MUST propagate (ID changes from 200 to 100)
        CHECK(moved.get_allocator().id == 100);

        // Fast buffer stealing path verification
        CHECK(moved.data() == original_ptr);
        CHECK(moved.size() == 3);

        // Source state tracking
        CHECK(original.data() == nullptr);
        CHECK(original.size() == 0);
    }

    SECTION("Path 2: POCMA is false, but allocators compare equal - steals buffer, leaves "
            "allocator intact")
    {
        // Set PropagateMove = false, AlwaysEqual = false (but matching instances)
        using AllocType = CustomTestAllocator<int, false, false>;

        forge::vector<int, AllocType> original({1, 2, 3}, AllocType(50));
        forge::vector<int, AllocType> moved({7, 8, 9, 10}, AllocType(50)); // Same ID!

        int* original_ptr = original.data();

        moved = std::move(original);

        // Allocator remains our own instance
        CHECK(moved.get_allocator().id == 50);

        // Fast path buffer verification
        CHECK(moved.data() == original_ptr);
        CHECK(moved.size() == 3);

        // Source state tracking
        CHECK(original.data() == nullptr);
        CHECK(original.size() == 0);
    }

    SECTION(
        "Path 3: Fallback - POCMA is false and allocators are UNEQUAL (Element-wise dynamic move)")
    {
        // Set PropagateMove = false, AlwaysEqual = false
        using AllocType = CustomTestAllocator<int, false, false>;

        forge::vector<int, AllocType> original({10, 20, 30}, AllocType(11));
        forge::vector<int, AllocType> moved({1, 2}, AllocType(22)); // Differing IDs!

        int* original_ptr = original.data();
        int* moved_old_ptr = moved.data();

        moved = std::move(original);

        // 1. Allocator must NOT propagate
        CHECK(moved.get_allocator().id == 22);

        // 2. Memory blocks must NOT be stolen (since cross-allocator pointer deletion is fatal)
        CHECK(moved.data() != original_ptr);
        CHECK(original.data() == original_ptr); // Source retains its pointer block!

        // 3. Values were successfully drained out element-by-element
        REQUIRE(moved.size() == 3);
        CHECK(moved[0] == 10);
        CHECK(moved[2] == 30);

        // 4. Source vector state verification on element fallback
        CHECK(original.size() == 3); // Size stays intact per standard element move specifications
    }

    SECTION("Conditional noexcept contract matching specifications")
    {
        // Case A: Propagating or AlwaysEqual allocators MUST evaluate to noexcept
        using SafeAlloc = CustomTestAllocator<int, true, false>;
        STATIC_REQUIRE(noexcept(std::declval<forge::vector<int, SafeAlloc>&>() =
                                    std::declval<forge::vector<int, SafeAlloc>>()));

        // Case B: Non-propagating, non-equal allocators are NOT noexcept because
        // element-wise fallback assignment can trigger memory reallocations
        using FallbackAlloc = CustomTestAllocator<int, false, false>;
        STATIC_REQUIRE_FALSE(noexcept(std::declval<forge::vector<int, FallbackAlloc>&>() =
                                          std::declval<forge::vector<int, FallbackAlloc>>()));
    }
}

TEST_CASE("Vector operator[]", "[vector][element access]")
{
    SECTION("non-const")
    {
        forge::vector<int> vec{1, 2, 3, 4, 5};

        SECTION("returns a reference to the first element when index is 0")
        {
            CHECK(vec[0] == 1);
        }

        SECTION("returns a reference to the last element when index is size() - 1")
        {
            CHECK(vec[vec.size() - 1] == 5);
        }

        SECTION("returns a reference to the correct element for valid indices")
        {
            for (std::size_t i = 0; i < vec.size(); ++i)
            {
                CHECK(vec[i] == static_cast<int>(i + 1));
            }
        }

        SECTION("allows modifying elements through the returned reference")
        {
            vec[0] = 42;
            CHECK(vec[0] == 42); // The first element should have been modified
            for (std::size_t i = 1; i < vec.size(); ++i)
            {
                CHECK(vec[i] ==
                      static_cast<int>(i + 1)); // The other elements should remain unchanged
            }
        }

        SECTION("honors the noexcept contract")
        {
            STATIC_REQUIRE(noexcept(std::declval<forge::vector<int>&>()[0]));
        }
    }

    SECTION("const")
    {
        const forge::vector<int> vec{1, 2, 3, 4, 5};

        SECTION("returns a reference to the first element when index is 0")
        {
            CHECK(vec[0] == 1);
        }

        SECTION("returns a reference to the last element when index is size() - 1")
        {
            CHECK(vec[vec.size() - 1] == 5);
        }

        SECTION("returns a reference to the correct element for valid indices")
        {
            for (std::size_t i = 0; i < vec.size(); ++i)
            {
                CHECK(vec[i] == static_cast<int>(i + 1));
            }
        }

        SECTION("honors the noexcept contract")
        {
            STATIC_REQUIRE(noexcept(std::declval<forge::vector<int>&>()[0]));
        }
    }
}

TEST_CASE("Vector at()", "[vector][element access]")
{
    // Similar structure to operator[] tests, but also includes checks for out-of-range access
    // throwing std::out_of_range and that the exception message is correct.
    SECTION("non-const")
    {
        forge::vector<int> vec{1, 2, 3, 4, 5};

        SECTION("returns a reference to the first element when index is 0")
        {
            CHECK(vec.at(0) == 1);
        }

        SECTION("returns a reference to the last element when index is size() - 1")
        {
            CHECK(vec.at(vec.size() - 1) == 5);
        }

        SECTION("returns a reference to the correct element for valid indices")
        {
            for (std::size_t i = 0; i < vec.size(); ++i)
            {
                CHECK(vec.at(i) == static_cast<int>(i + 1));
            }
        }

        SECTION("allows modifying elements through the returned reference")
        {
            vec.at(0) = 42;
            CHECK(vec.at(0) == 42); // The first element should have been modified
            for (std::size_t i = 1; i < vec.size(); ++i)
            {
                CHECK(vec.at(i) ==
                      static_cast<int>(i + 1)); // The other elements should remain unchanged
            }
        }

        SECTION("throws std::out_of_range for out-of-range access")
        {
            REQUIRE_THROWS_AS(vec.at(vec.size()),
                              std::out_of_range); // Accessing one past the end should throw
            REQUIRE_THROWS_AS(vec.at(-1),
                              std::out_of_range); // Accessing a negative index should throw
        }
    }

    SECTION("const")
    {
        const forge::vector<int> vec{1, 2, 3, 4, 5};

        SECTION("returns a reference to the first element when index is 0")
        {
            CHECK(vec.at(0) == 1);
        }

        SECTION("returns a reference to the last element when index is size() - 1")
        {
            CHECK(vec.at(vec.size() - 1) == 5);
        }

        SECTION("returns a reference to the correct element for valid indices")
        {
            for (std::size_t i = 0; i < vec.size(); ++i)
            {
                CHECK(vec.at(i) == static_cast<int>(i + 1));
            }
        }

        SECTION("throws std::out_of_range for out-of-range access")
        {
            REQUIRE_THROWS_AS(vec.at(vec.size()),
                              std::out_of_range); // Accessing one past the end should throw
            REQUIRE_THROWS_AS(vec.at(-1),
                              std::out_of_range); // Accessing a negative index should throw
        }
    }
}

TEST_CASE("Vector front()", "[vector][element access]")
{
    SECTION("non-const")
    {
        forge::vector<int> vec{1, 2, 3, 4, 5};

        SECTION("returns a reference to the first element")
        {
            CHECK(vec.front() == 1);
        }

        SECTION("allows modifying the first element through the returned reference")
        {
            vec.front() = 42;
            CHECK(vec.front() == 42); // The first element should have been modified
            for (std::size_t i = 1; i < vec.size(); ++i)
            {
                CHECK(vec[i] ==
                      static_cast<int>(i + 1)); // The other elements should remain unchanged
            }
        }

        SECTION("honors the noexcept contract")
        {
            STATIC_REQUIRE(noexcept(std::declval<forge::vector<int>&>().front()));
        }
    }

    SECTION("const")
    {
        const forge::vector<int> vec{1, 2, 3, 4, 5};

        SECTION("returns a reference to the first element")
        {
            CHECK(vec.front() == 1);
        }

        SECTION("honors the noexcept contract")
        {
            STATIC_REQUIRE(noexcept(std::declval<const forge::vector<int>&>().front()));
        }
    }
}

TEST_CASE("Vector back()", "[vector][element access]")
{
    SECTION("non-const")
    {
        forge::vector<int> vec{1, 2, 3, 4, 5};

        SECTION("returns a reference to the last element")
        {
            CHECK(vec.back() == 5);
        }

        SECTION("allows modifying the last element through the returned reference")
        {
            vec.back() = 42;
            CHECK(vec.back() == 42); // The last element should have been modified
            for (std::size_t i = 0; i < vec.size() - 1; ++i)
            {
                CHECK(vec[i] ==
                      static_cast<int>(i + 1)); // The other elements should remain unchanged
            }
        }

        SECTION("honors the noexcept contract")
        {
            STATIC_REQUIRE(noexcept(std::declval<forge::vector<int>&>().back()));
        }
    }

    SECTION("const")
    {
        const forge::vector<int> vec{1, 2, 3, 4, 5};

        SECTION("returns a reference to the last element")
        {
            CHECK(vec.back() == 5);
        }

        SECTION("honors the noexcept contract")
        {
            STATIC_REQUIRE(noexcept(std::declval<const forge::vector<int>&>().back()));
        }
    }
}

TEST_CASE("Vector data()", "[vector][element access]")
{
    SECTION("non-const")
    {
        forge::vector<int> vec{1, 2, 3, 4, 5};

        SECTION("returns a pointer to the underlying array")
        {
            CHECK(vec.data() != nullptr);
            for (std::size_t i = 0; i < vec.size(); ++i)
            {
                CHECK(vec.data()[i] ==
                      static_cast<int>(i + 1)); // Elements should match the vector's elements
            }
        }

        SECTION("returns nullptr for an empty vector")
        {
            forge::vector<int> empty_vec;
            CHECK(empty_vec.data() == nullptr);
        }

        SECTION("allows modifying elements through the returned pointer")
        {
            int* data_ptr = vec.data();
            data_ptr[0] = 42;
            CHECK(vec[0] == 42); // The first element should have been modified
            for (std::size_t i = 1; i < vec.size(); ++i)
            {
                CHECK(vec[i] ==
                      static_cast<int>(i + 1)); // The other elements should remain unchanged
            }
        }

        SECTION("honors the noexcept contract")
        {
            STATIC_REQUIRE(noexcept(std::declval<forge::vector<int>&>().data()));
        }
    }

    SECTION("const")
    {
        const forge::vector<int> vec{1, 2, 3, 4, 5};

        SECTION("returns a pointer to the underlying array")
        {
            CHECK(vec.data() != nullptr);
            for (std::size_t i = 0; i < vec.size(); ++i)
            {
                CHECK(vec.data()[i] ==
                      static_cast<int>(i + 1)); // Elements should match the vector's elements
            }
        }

        SECTION("returns nullptr for an empty vector")
        {
            const forge::vector<int> empty_vec;
            CHECK(empty_vec.data() == nullptr);
        }

        SECTION("honors the noexcept contract")
        {
            STATIC_REQUIRE(noexcept(std::declval<const forge::vector<int>&>().data()));
        }
    }
}

TEST_CASE("Vector get_view()", "[vector][element access]")
{
    SECTION("non-const")
    {
        forge::vector<int> vec{1, 2, 3, 4, 5};

        SECTION("returns a view of the vector's elements")
        {
            auto view = vec.get_view();
            CHECK(view.size() == vec.size());
            for (std::size_t i = 0; i < view.size(); ++i)
            {
                CHECK(view[i] ==
                      static_cast<int>(i + 1)); // Elements should match the vector's elements
            }
        }

        SECTION("allows modifying elements through the returned view")
        {
            auto view = vec.get_view();
            view[0] = 42;
            CHECK(vec[0] == 42); // The first element should have been modified
            for (std::size_t i = 1; i < vec.size(); ++i)
            {
                CHECK(vec[i] ==
                      static_cast<int>(i + 1)); // The other elements should remain unchanged
            }
        }

        SECTION("returns an empty view for an empty vector")
        {
            forge::vector<int> empty_vec;
            auto view = empty_vec.get_view();
            CHECK(view.size() == 0);
        }

        SECTION("honors the noexcept contract")
        {
            STATIC_REQUIRE(noexcept(std::declval<forge::vector<int>&>().get_view()));
        }
    }

    SECTION("const")
    {
        const forge::vector<int> vec{1, 2, 3, 4, 5};

        SECTION("returns a view of the vector's elements")
        {
            auto view = vec.get_view();
            CHECK(view.size() == vec.size());
            for (std::size_t i = 0; i < view.size(); ++i)
            {
                CHECK(view[i] ==
                      static_cast<int>(i + 1)); // Elements should match the vector's elements
            }
        }

        SECTION("returns an empty view for an empty vector")
        {
            const forge::vector<int> empty_vec;
            auto view = empty_vec.get_view();
            CHECK(view.size() == 0);
        }

        SECTION("honors the noexcept contract")
        {
            STATIC_REQUIRE(noexcept(std::declval<const forge::vector<int>&>().get_view()));
        }
    }
}

template <typename T> constexpr bool test_constexpr_contains()
{
    forge::vector<T> v;
    v.push_back(T{10});
    v.push_back(T{20});
    v.push_back(T{30});
    return v.contains(T{20}) && !v.contains(T{99});
}

TEST_CASE("Vector contains()", "[vector][element access]")
{
    forge::vector<int> vec{1, 2, 3, 4, 5};

    SECTION("returns true for elements at the beginning, middle, and end of the vector")
    {
        CHECK(vec.contains(1));
        CHECK(vec.contains(3));
        CHECK(vec.contains(5));
    }

    SECTION("returns false for elements that are not in the vector")
    {
        CHECK_FALSE(vec.contains(0));
        CHECK_FALSE(vec.contains(6));
        CHECK_FALSE(vec.contains(-1));
    }

    SECTION("returns false for an empty vector")
    {
        forge::vector<int> empty_vec;
        CHECK_FALSE(empty_vec.contains(1)); // No elements should be found in an empty vector
    }

    SECTION("returns true for duplicate elements in the vector")
    {
        forge::vector<int> vec_with_duplicates{1, 2, 3, 2, 4, 5};
        CHECK(
            vec_with_duplicates.contains(2)); // The contains() method should return true if the
                                              // element is found, even if it appears multiple times
    }

    SECTION("works for const vectors")
    {
        const forge::vector<int> const_vec{1, 2, 3, 4, 5};
        CHECK(const_vec.contains(1));
        CHECK(const_vec.contains(3));
        CHECK(const_vec.contains(5));
        CHECK_FALSE(const_vec.contains(0));
    }

    SECTION("Optimized path: 1-byte trivial types (via char_traits)")
    {
        // Triggers the sizeof(T) == 1 && is_trivially_copyable branch
        forge::vector<uint8_t> byte_vec{10, 20, 30, 42, 50};

        CHECK(byte_vec.contains(uint8_t{10}));       // Beginning
        CHECK(byte_vec.contains(uint8_t{42}));       // Middle
        CHECK(byte_vec.contains(uint8_t{50}));       // End
        CHECK_FALSE(byte_vec.contains(uint8_t{99})); // Missing

        // Check with char as well
        forge::vector<char> char_vec{'f', 'o', 'r', 'g', 'e'};
        CHECK(char_vec.contains('o'));
        CHECK_FALSE(char_vec.contains('z'));
    }

    SECTION("Heterogeneous lookup: Searches with compatible types")
    {
        // Case A: string vs string_view (Prevents temporary std::string allocations)
        forge::vector<std::string> string_vec{"apple", "banana", "cherry"};

        // Passing std::string_view instead of const std::string&
        std::string_view search_view = "banana";
        std::string_view missing_view = "durian";

        CHECK(string_vec.contains(search_view));
        CHECK_FALSE(string_vec.contains(missing_view));
        CHECK(string_vec.contains("apple")); // Raw string literal lookup

        // Case B: Numeric cross-type comparisons (int vs double)
        forge::vector<double> double_vec{1.0, 2.5, 3.0, 4.25};
        CHECK(double_vec.contains(3)); // int lookup against double container
        CHECK_FALSE(double_vec.contains(5));
    }

    SECTION("Compiles and runs correctly in a constexpr context")
    {
        // Tests the 1-byte char_traits path at compile-time
        STATIC_REQUIRE(test_constexpr_contains<uint8_t>());
        STATIC_REQUIRE(test_constexpr_contains<char>());

        // Tests the general std::ranges::find path at compile-time
        STATIC_REQUIRE(test_constexpr_contains<int>());
        STATIC_REQUIRE(test_constexpr_contains<double>());
    }

    SECTION("honors the noexcept contract and respects concept constraints")
    {
        // Base noexcept condition
        STATIC_REQUIRE(noexcept(std::declval<forge::vector<int>&>().contains(0)));

        // Verifies the constraint works: checking if contains detects type-compatibility
        // constraints (A vector of ints cannot be queried with a std::string)
        STATIC_REQUIRE(!std::equality_comparable_with<int, std::string>);
    }
}

TEST_CASE("Vector find()", "[vector][element access]")
{
    forge::vector<int> vec{1, 2, 3, 4, 5};

    SECTION("returns an iterator to the first occurrence of the specified element")
    {
        auto it = vec.find(3);
        REQUIRE(it != vec.end());
        CHECK(*it == 3);
    }

    SECTION("returns end() if the specified element is not found")
    {
        auto it = vec.find(0);
        CHECK(it == vec.end());
    }

    SECTION("returns end() for an empty vector")
    {
        forge::vector<int> empty_vec;
        auto it = empty_vec.find(1);
        CHECK(it == empty_vec.end());
    }

    SECTION("works for const vectors")
    {
        const forge::vector<int> const_vec{1, 2, 3, 4, 5};
        auto it = const_vec.find(3);
        REQUIRE(it != const_vec.end());
        CHECK(*it == 3);

        auto it_not_found = const_vec.find(0);
        CHECK(it_not_found == const_vec.end());
    }

    SECTION("honors the noexcept contract")
    {
        STATIC_REQUIRE(noexcept(std::declval<forge::vector<int>&>().find(0)));
    }
}

TEST_CASE("Vector get_allocator()", "[vector][allocator]")
{
    SECTION("returns std::allocator for default allocator")
    {
        forge::vector<int> vec;
        auto alloc = vec.get_allocator();
        CHECK(typeid(alloc) ==
              typeid(std::allocator<int>)); // The allocator type should be std::allocator<int>
    }

    SECTION("returns the correct allocator for a custom allocator")
    {
        IdAllocator<int> id_alloc(42);
        forge::vector<int, IdAllocator<int>> vec(id_alloc);
        auto alloc = vec.get_allocator();
        CHECK(typeid(alloc) ==
              typeid(IdAllocator<int>)); // The allocator type should be IdAllocator<int>
        CHECK(alloc.id == 42);           // The allocator's ID should match the one we provided
    }

    SECTION("works for const vectors")
    {
        forge::vector<int> vec;
        const auto& const_vec = vec;
        auto alloc = const_vec.get_allocator();
        CHECK(typeid(alloc) ==
              typeid(std::allocator<int>)); // The allocator type should be std::allocator<int>
    }

    SECTION("honors the noexcept contract")
    {
        forge::vector<int> vec;
        STATIC_REQUIRE(noexcept(vec.get_allocator()));
    }
}

TEST_CASE("Vector size()", "[vector][capacity]")
{
    SECTION("returns the number of elements in the vector")
    {
        forge::vector<int> vec{1, 2, 3, 4, 5};
        CHECK(vec.size() == 5);
    }

    SECTION("returns 0 for an empty vector")
    {
        forge::vector<int> empty_vec;
        CHECK(empty_vec.size() == 0);
    }

    SECTION("works for const vectors")
    {
        const forge::vector<int> const_vec{1, 2, 3, 4, 5};
        CHECK(const_vec.size() == 5);

        const forge::vector<int> empty_const_vec;
        CHECK(empty_const_vec.size() == 0);
    }

    SECTION("honors the noexcept contract")
    {
        forge::vector<int> vec;
        STATIC_REQUIRE(noexcept(vec.size()));
    }
}

TEST_CASE("Vector max_size()", "[vector][capacity]")
{
    SECTION("returns the maximum number of elements that the vector can hold")
    {
        forge::vector<int> vec;
        CHECK(vec.max_size() > 0); // The max_size should be greater than 0
    }

    SECTION("works for const vectors")
    {
        const forge::vector<int> const_vec;
        CHECK(const_vec.max_size() > 0); // The max_size should be greater than 0
    }

    SECTION("honors the noexcept contract")
    {
        forge::vector<int> vec;
        STATIC_REQUIRE(noexcept(vec.max_size()));
    }
}

TEST_CASE("Vector capacity()", "[vector][capacity]")
{
    SECTION("returns the number of elements that can be held in currently allocated storage")
    {
        forge::vector<int> vec{1, 2, 3, 4, 5};
        CHECK(vec.capacity() >=
              vec.size()); // The capacity should be greater than or equal to the size

        vec.push_back(6);
        CHECK(vec.capacity() >= vec.size()); // The capacity should still be greater than or equal
    }

    SECTION("returns 0 for an empty vector")
    {
        forge::vector<int> empty_vec;
        CHECK(empty_vec.capacity() == 0);
    }

    SECTION("works for const vectors")
    {
        const forge::vector<int> const_vec{1, 2, 3, 4, 5};
        CHECK(const_vec.capacity() >=
              const_vec.size()); // The capacity should be greater than or equal to the size

        const forge::vector<int> empty_const_vec;
        CHECK(empty_const_vec.capacity() == 0);
    }

    SECTION("honors the noexcept contract")
    {
        forge::vector<int> vec;
        STATIC_REQUIRE(noexcept(vec.capacity()));
    }
}

TEST_CASE("Vector empty()", "[vector][capacity]")
{
    SECTION("returns true if the vector has no elements")
    {
        forge::vector<int> empty_vec;
        CHECK(empty_vec.empty());
    }

    SECTION("returns false if the vector has one or more elements")
    {
        forge::vector<int> vec{1, 2, 3};
        CHECK_FALSE(vec.empty());
    }

    SECTION("works for const vectors")
    {
        const forge::vector<int> empty_const_vec;
        CHECK(empty_const_vec.empty());

        const forge::vector<int> const_vec{1, 2, 3};
        CHECK_FALSE(const_vec.empty());
    }

    SECTION("honors the noexcept contract")
    {
        forge::vector<int> vec;
        STATIC_REQUIRE(noexcept(vec.empty()));
    }
}

TEST_CASE("Vector memory_usage()", "[vector][capacity]")
{
    SECTION("returns the total memory usage of the vector, including allocated but unused capacity")
    {
        forge::vector<char> vec(100);
        std::size_t expected_usage = sizeof(char) * vec.capacity();
        CHECK(vec.memory_usage() == expected_usage);

        forge::vector<double> vec2(50);
        expected_usage = sizeof(double) * vec2.capacity();
        CHECK(vec2.memory_usage() == expected_usage);
    }

    SECTION("returns 0 for an empty vector")
    {
        forge::vector<int> empty_vec;
        CHECK(empty_vec.memory_usage() == 0);
    }

    SECTION("correctly accounts for the control block overhead when requested")
    {
        forge::vector<int> vec(20);
        std::size_t raw_buffer_bytes = sizeof(int) * vec.capacity();
        std::size_t total_expected_bytes = raw_buffer_bytes + sizeof(vec);

        // Explicitly requesting control block inclusion
        CHECK(vec.memory_usage(true) == total_expected_bytes);

        // Verifying default parameter maps back to the raw buffer only
        CHECK(vec.memory_usage(false) == raw_buffer_bytes);
        CHECK(vec.memory_usage() == raw_buffer_bytes);
    }

    SECTION("works for const vectors")
    {
        const forge::vector<int> const_vec{1, 2, 3, 4, 5};
        std::size_t expected_usage = sizeof(int) * const_vec.capacity();
        CHECK(const_vec.memory_usage() == expected_usage);

        const forge::vector<int> empty_const_vec;
        CHECK(empty_const_vec.memory_usage() == 0);
    }

    SECTION("honors the noexcept contract")
    {
        forge::vector<int> vec;
        STATIC_REQUIRE(noexcept(vec.memory_usage()));
    }
}

TEST_CASE("Vector reserve()", "[vector][capacity]")
{
    SECTION("increases the capacity of the vector to the specified amount while preserving "
            "existing elements")
    {
        forge::vector<int> vec{1, 2, 3, 4, 5};
        vec.reserve(100);
        CHECK(vec.capacity() >= 100);
        CHECK(vec.size() == 5); // The size should remain unchanged
        REQUIRE(vec.data() != nullptr);
        for (std::size_t i = 0; i < vec.size(); ++i)
        {
            CHECK(vec[i] == static_cast<int>(i + 1)); // Existing elements should be preserved
        }
    }

    SECTION("does not decrease the capacity if the specified amount is equal to or less than the "
            "current capacity")
    {
        forge::vector<int> vec(100);
        std::size_t current_capacity = vec.capacity();
        vec.reserve(50); // Reserving less than current capacity should not decrease it
        CHECK(vec.capacity() == current_capacity);
        vec.reserve(100); // Reserving equal to current capacity should not decrease it
        CHECK(vec.capacity() == current_capacity);
    }

    SECTION("throws std::length_error if the requested capacity exceeds max_size()")
    {
        forge::vector<int> vec;
        REQUIRE_THROWS_AS(vec.reserve(vec.max_size() + 1), std::length_error);
    }

    SECTION("handles allocation failures")
    {
        ToggleAllocator<int>::should_fail =
            false; // Ensure that the allocator is not set to fail before creating the vector
        forge::vector<int, ToggleAllocator<int>> vec(
            5, 1,
            ToggleAllocator<int>{}); // Create a vector with 5 elements using the ToggleAllocator
        ToggleAllocator<int>::should_fail = true; // Set the allocator to fail on allocation
        REQUIRE_THROWS_AS(vec.reserve(10), std::bad_alloc);

        // After a failed allocation, the vector should still be in a valid state with its original
        // capacity and size
        CHECK(vec.size() == 5);
        CHECK(vec.capacity() == 5);
        REQUIRE(vec.data() != nullptr);
        for (std::size_t i = 0; i < vec.size(); ++i)
        {
            CHECK(vec[i] == 1); // Existing elements should still be intact after allocation failure
        }
    }

    SECTION("honors the exception safety guarantees")
    {
        ThrowsAfterNConstructions::reset(-1); // Disable throwing so we can create the vector
        forge::vector<ThrowsAfterNConstructions> vec(5, 1);
        ThrowsAfterNConstructions::reset(2); // Set the constructor to throw after 2 constructions

        REQUIRE_THROWS_AS(vec.reserve(15), std::runtime_error);
        CHECK(ThrowsAfterNConstructions::count ==
              0); // All constructed elements should have been destroyed
    }
}

TEST_CASE("Vector shrink_to_fit()", "[vector][capacity]")
{
    SECTION(
        "reduces the capacity of the vector to match its size while preserving existing elements")
    {
        forge::vector<int> vec{1, 2, 3, 4, 5};
        vec.reserve(
            100); // Increase capacity to create a scenario where shrink_to_fit would have an effect
        vec.shrink_to_fit();
        CHECK(vec.capacity() == vec.size()); // Capacity should be equal to size after shrink_to_fit
        REQUIRE(vec.data() != nullptr);
        for (std::size_t i = 0; i < vec.size(); ++i)
        {
            CHECK(vec[i] == static_cast<int>(i + 1)); // Existing elements should be preserved
        }
    }

    SECTION("does not change the capacity if it is already equal to the size")
    {
        forge::vector<int> vec{1, 2, 3, 4, 5};
        std::size_t current_capacity = vec.capacity();
        vec.shrink_to_fit(); // Shrinking when capacity is already equal to size should not change
                             // it
        CHECK(vec.capacity() == current_capacity);
    }

    SECTION("works for empty vectors")
    {
        forge::vector<int> empty_vec;
        empty_vec.shrink_to_fit(); // Shrinking an empty vector should not cause any issues
        CHECK(empty_vec.capacity() == 0);
        CHECK(empty_vec.data() == nullptr);
    }

    SECTION("handles allocation failures")
    {
        ToggleAllocator<int>::should_fail =
            false; // Ensure that the allocator is not set to fail before creating the vector
        forge::vector<int, ToggleAllocator<int>> vec(
            5, 1,
            ToggleAllocator<int>{}); // Create a vector with 5 elements using the ToggleAllocator
        vec.reserve(
            100); // Increase capacity to create a scenario where shrink_to_fit would have an effect
        ToggleAllocator<int>::should_fail = true; // Set the allocator to fail on allocation
        REQUIRE_THROWS_AS(vec.shrink_to_fit(), std::bad_alloc);

        // After a failed allocation, the vector should still be in a valid state with its original
        // capacity and size
        CHECK(vec.size() == 5);
        CHECK(vec.capacity() == 100);
        REQUIRE(vec.data() != nullptr);
        for (std::size_t i = 0; i < vec.size(); ++i)
        {
            CHECK(vec[i] == 1); // Existing elements should still be intact after allocation failure
        }
    }

    SECTION("honors the exception safety guarantees")
    {
        ThrowsAfterNConstructions::reset(-1); // Disable throwing so we can create the vector
        forge::vector<ThrowsAfterNConstructions> vec(5, 1);
        vec.reserve(
            100); // Increase capacity to create a scenario where shrink_to_fit would have an effect
        ThrowsAfterNConstructions::reset(2); // Set the constructor to throw after 2 constructions

        REQUIRE_THROWS_AS(vec.shrink_to_fit(), std::runtime_error);
        CHECK(ThrowsAfterNConstructions::count ==
              0); // All constructed elements should have been destroyed
    }
}

TEST_CASE("Vector push_back()", "[vector][modifiers]")
{
    // push_back() uses emplace_back() internally, so we only need to test that push_back()
    // correctly forwards to emplace_back() and that it behaves as expected in terms of copying and
    // moving elements. The detailed behavior of adding elements and handling reallocations will be
    // covered in the emplace_back() tests.

    SECTION("forwards to emplace_back() to add an element to the end of the vector")
    {
        forge::vector<int> vec{0, 1};
        vec.push_back(2);
        CHECK(vec.size() == 3);
        CHECK(vec.back() == 2);

        // Check that existing elements are preserved
        CHECK(vec[0] == 0);
        CHECK(vec[1] == 1);
        CHECK(vec[2] == 2);
    }

    SECTION("with Lvalue copies the element into the vector")
    {
        forge::vector<std::string> vec;
        std::string str = "Hello";
        vec.push_back(str); // Should copy str into the vector
        CHECK(vec.size() == 1);
        CHECK(vec[0] == "Hello");
        CHECK(str == "Hello"); // The original string should remain unchanged
    }

    SECTION("with Rvalue moves the element into the vector")
    {
        forge::vector<std::string> vec;
        std::string str = "Hello";
        vec.push_back(std::move(str)); // Should move str into the vector
        CHECK(vec.size() == 1);
        CHECK(vec[0] == "Hello");
        CHECK(str.empty()); // The original string should be moved from
    }
}

TEST_CASE("Vector emplace_back()", "[vector][modifiers]")
{
    SECTION("adds an element to the end of the vector, increasing its size by one while preserving "
            "existing elements")
    {
        forge::vector<int> vec{0, 1};
        vec.emplace_back(2);
        CHECK(vec.size() == 3);
        CHECK(vec.back() == 2);

        // Check that existing elements are preserved
        CHECK(vec[0] == 0);
        CHECK(vec[1] == 1);
        CHECK(vec[2] == 2);
    }

    SECTION("perfectly forwards arguments to construct the new element in place")
    {
        forge::vector<std::pair<int, std::string>> vec;
        vec.emplace_back(
            42, "Hello"); // Should perfectly forward the arguments to construct the pair in place
        CHECK(vec.size() == 1);
        CHECK(vec[0].first == 42);
        CHECK(vec[0].second == "Hello");
    }

    SECTION("constructs elements in place")
    {
        forge::vector<std::string> vec;
        vec.emplace_back(5, 'a'); // Should construct a string of 5 'a' characters in place
        CHECK(vec.size() == 1);
        CHECK(vec[0] == "aaaaa");
    }

    SECTION("triggers a reallocation of 1.5x if the size exceeds the current capacity")
    {
        forge::vector<int> vec(10, 0);
        vec.emplace_back(1); // This should trigger a reallocation since the size will exceed the
                             // initial capacity of 10
        CHECK(vec.capacity() ==
              15); // After reallocation, the capacity should be at 1.5x the previous capacity
    }

    SECTION("does not trigger a reallocation if the size does not exceed the current capacity")
    {
        forge::vector<int> vec;
        vec.reserve(10); // Reserve capacity for 10 elements
        vec.emplace_back(1);
        CHECK(vec.capacity() == 10);
    }

    SECTION("honors the exception safety guarantees")
    {
        ThrowsAfterNConstructions::reset(-1); // Disable throwing so we can create the vector
        forge::vector<ThrowsAfterNConstructions> vec(5, 1);
        ThrowsAfterNConstructions::reset(5); // Set the constructor to throw immediately

        REQUIRE_THROWS_AS(vec.emplace_back(ThrowsAfterNConstructions()), std::runtime_error);
        CHECK(ThrowsAfterNConstructions::count ==
              0); // All constructed elements should have been destroyed

        // After a failed emplace_back, the vector should still be in a valid state with its
        // original size
        CHECK(vec.size() == 5);
    }
}

TEST_CASE("Vector insert(pos, value)", "[vector][modifiers]")
{
    // insert() uses emplace() internally, so we only need to test that insert() correctly forwards
    // to emplace() and that it behaves as expected in terms of copying and moving elements. The
    // detailed behavior of adding elements and handling reallocations will be covered in the
    // emplace() tests.

    SECTION("forwards to emplace() to add an element at the specified position in the vector")
    {
        forge::vector<int> vec{1, 2, 4, 5};
        auto it = vec.insert(vec.begin() + 2, 3); // Insert 3 at index 2
        CHECK(vec.size() == 5);
        CHECK(*it == 3); // The returned iterator should point to the newly inserted element
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 2);
        CHECK(vec[2] == 3); // The new element should be at index
        CHECK(vec[3] == 4);
        CHECK(vec[4] == 5);
    }

    SECTION("with Lvalue copies the element into the vector")
    {
        forge::vector<std::string> vec{"Hello", "!"};
        std::string str = "World";
        auto it = vec.insert(vec.begin() + 1, str); // Should copy str into the vector
        CHECK(vec.size() == 3);
        CHECK(*it == "World");
        CHECK(str == "World"); // The original string should remain unchanged
    }

    SECTION("with Rvalue moves the element into the vector")
    {
        forge::vector<std::string> vec{"Hello", "!"};
        std::string str = "World";
        auto it = vec.insert(vec.begin() + 1, std::move(str)); // Should move str into the vector
        CHECK(vec.size() == 3);
        CHECK(*it == "World");
        CHECK(str.empty()); // The original string should be moved from
    }
}

TEST_CASE("Vector insert(pos, count, value)", "[vector][modifiers]")
{
    forge::vector<int> vec{1, 2, 5};

    SECTION("adds count copies of the specified value to any position in the vector, increasing "
            "its size by count while preserving existing elements")
    {
        SECTION("at the beginning")
        {
            auto it = vec.insert(vec.begin(), 2, 0); // Insert two 0s at the beginning
            CHECK(it == vec.begin()); // The returned iterator should point to the first newly
                                      // inserted element
            CHECK(vec.size() == 5);
            CHECK(vec[0] == 0);
            CHECK(vec[1] == 0);
            CHECK(vec[2] == 1);
            CHECK(vec[3] == 2);
            CHECK(vec[4] == 5);
        }

        SECTION("in the middle")
        {
            auto it = vec.insert(vec.begin() + 2, 2, 3); // Insert two 3s in the middle
            CHECK(it ==
                  vec.begin() +
                      2); // The returned iterator should point to the first newly inserted element
            CHECK(vec.size() == 5);
            CHECK(vec[0] == 1);
            CHECK(vec[1] == 2);
            CHECK(vec[2] == 3);
            CHECK(vec[3] == 3);
            CHECK(vec[4] == 5);
        }

        SECTION("at the end")
        {
            auto it = vec.insert(vec.end(), 2, 6); // Insert two 6s at the end
            CHECK(it ==
                  vec.begin() +
                      3); // The returned iterator should point to the first newly inserted element
            CHECK(vec.size() == 5);
            CHECK(vec[0] == 1);
            CHECK(vec[1] == 2);
            CHECK(vec[2] == 5);
            CHECK(vec[3] == 6);
            CHECK(vec[4] == 6);
        }
    }

    SECTION("adds one copy of the specified value if count is 1")
    {
        auto it = vec.insert(vec.begin() + 2, 1, 3); // Insert one 3 in the middle
        CHECK(it ==
              vec.begin() + 2); // The returned iterator should point to the newly inserted element
        CHECK(vec.size() == 4);
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 2);
        CHECK(vec[2] == 3);
        CHECK(vec[3] == 5);
    }

    SECTION("returns an iterator to original pos if count is 0")
    {
        auto it = vec.insert(vec.begin() + 2, 0, 3); // Insert zero 3s in the middle
        CHECK(it == vec.begin() + 2); // Should return an iterator to the original position
        CHECK(vec.size() == 3);       // Size should remain unchanged
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 2);
        CHECK(vec[2] == 5);
    }

    SECTION("adds count copies of the specified value to an empty vector")
    {
        forge::vector<int> empty_vec;
        auto it =
            empty_vec.insert(empty_vec.begin(), 3, 42); // Insert three 42s into the empty vector
        CHECK(it == empty_vec.begin()); // The returned iterator should point to the first newly
                                        // inserted element
        CHECK(empty_vec.size() == 3);
        CHECK(empty_vec[0] == 42);
        CHECK(empty_vec[1] == 42);
        CHECK(empty_vec[2] == 42);
    }

    SECTION("throws std::length_error if the new size would exceed max_size()")
    {
        REQUIRE_THROWS_AS(vec.insert(vec.begin(), vec.max_size() + 1, 0), std::length_error);
    }

    SECTION("triggers a reallocation if the new size exceeds the current capacity")
    {
        forge::vector<int> vec(10, 0);
        SECTION("by 1.5x")
        {
            forge::vector<int> vec(10, 0);
            auto it =
                vec.insert(vec.end(), 2, 1); // This should trigger a reallocation since the new
                                             // size will exceed the initial capacity of 10
            CHECK(vec.capacity() ==
                  15); // After reallocation, the capacity should be 1.5x the previous capacity
            CHECK(it ==
                  vec.begin() +
                      10); // The returned iterator should point to the first newly inserted element
            for (std::size_t i = 0; i < 10; ++i)
            {
                CHECK(vec[i] == 0); // Existing elements should be preserved
            }
            for (std::size_t i = 10; i < vec.size(); ++i)
            {
                CHECK(vec[i] == 1); // New elements should be correctly inserted
            }
        }

        SECTION("by size() + count")
        {
            auto it =
                vec.insert(vec.end(), 20, 1); // This should trigger a reallocation since the new
                                              // size will exceed the initial capacity of 10
            CHECK(vec.capacity() ==
                  30); // After reallocation, the capacity should be size() + count
            CHECK(it ==
                  vec.begin() +
                      10); // The returned iterator should point to the first newly inserted element
            for (std::size_t i = 0; i < 10; ++i)
            {
                CHECK(vec[i] == 0); // Existing elements should be preserved
            }
            for (std::size_t i = 10; i < vec.size(); ++i)
            {
                CHECK(vec[i] == 1); // New elements should be correctly inserted
            }
        }
    }

    SECTION("does not trigger a reallocation if the new size does not exceed the current capacity")
    {
        forge::vector<int> vec(10, 0);
        vec.reserve(20);
        auto it = vec.insert(vec.begin(), 2, 1); // Insert two 1s
        CHECK(vec.capacity() == 20);             // Capacity should remain unchanged
        CHECK(
            it ==
            vec.begin()); // The returned iterator should point to the first newly inserted element
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 1);
        CHECK(vec[2] == 0); // Existing elements should be preserved
    }

    SECTION("handles allocation failures")
    {
        ToggleAllocator<int>::should_fail =
            false; // Ensure that the allocator is not set to fail before creating the vector
        forge::vector<int, ToggleAllocator<int>> vec(
            5, 1,
            ToggleAllocator<int>{}); // Create a vector with 5 elements using the ToggleAllocator
        ToggleAllocator<int>::should_fail = true; // Set the allocator to fail on allocation
        REQUIRE_THROWS_AS(vec.insert(vec.end(), 2, 1), std::bad_alloc);

        // After a failed insertion, the vector should still be in a valid state with its original
        // size and capacity
        CHECK(vec.size() == 5);
        CHECK(vec.capacity() == 5);
        REQUIRE(vec.data() != nullptr);
        for (std::size_t i = 0; i < vec.size(); ++i)
        {
            CHECK(vec[i] == 1); // Existing elements should still be intact after allocation failure
        }
    }

    SECTION("honors the exception safety guarantees")
    {
        SECTION("when reallocation is needed")
        {
            ThrowsAfterNConstructions::reset(-1); // Disable throwing so we can create the vector
            forge::vector<ThrowsAfterNConstructions> vec(5, 1);
            ThrowsAfterNConstructions::reset(
                2); // Set the constructor to throw after 2 constructions

            REQUIRE_THROWS_AS(vec.insert(vec.end(), 10, ThrowsAfterNConstructions()),
                              std::runtime_error);
            CHECK(ThrowsAfterNConstructions::count ==
                  0); // All constructed elements should have been destroyed

            // After a failed insert, the vector should still be in a valid state with its original
            // size and capacity
            CHECK(vec.size() == 5);
            CHECK(vec.capacity() == 5);
        }

        SECTION("when no reallocation is needed")
        {
            ThrowsAfterNConstructions::reset(-1); // Disable throwing so we can create the vector
            forge::vector<ThrowsAfterNConstructions> vec(5);
            vec.reserve(20); // Ensure that no reallocation will be needed for the insert
            ThrowsAfterNConstructions::reset(
                2); // Set the constructor to throw after 2 constructions

            REQUIRE_THROWS_AS(vec.insert(vec.begin(), 5, ThrowsAfterNConstructions()),
                              std::runtime_error);
            CHECK(ThrowsAfterNConstructions::count ==
                  0); // All constructed elements should have been destroyed

            // After a failed insert, the vector should still be in a valid state with its original
            // size and capacity
            CHECK(vec.size() == 5);
            CHECK(vec.capacity() == 20);
        }
    }
}

TEST_CASE("Vector insert(pos, first, last)", "[vector][modifiers]")
{
    forge::vector<int> vec{1, 2, 5};

    SECTION("adds elements from the range [first, last) at the specified position in the vector, "
            "increasing its size by the number of elements in the range while preserving existing "
            "elements")
    {
        std::vector<int> to_insert{3, 4};

        SECTION("at the beginning")
        {
            auto it = vec.insert(vec.begin(), to_insert.begin(),
                                 to_insert.end()); // Insert 3 and 4 at the beginning
            CHECK(it == vec.begin()); // The returned iterator should point to the first newly
                                      // inserted element
            CHECK(vec.size() == 5);
            CHECK(vec[0] == 3); // The new elements should be at indices 0 and 1
            CHECK(vec[1] == 4);
            CHECK(vec[2] == 1);
            CHECK(vec[3] == 2);
            CHECK(vec[4] == 5);
        }

        SECTION("in the middle")
        {
            auto it = vec.insert(vec.begin() + 2, to_insert.begin(),
                                 to_insert.end()); // Insert 3 and 4 at index 2
            CHECK(it ==
                  vec.begin() +
                      2); // The returned iterator should point to the first newly inserted element
            CHECK(vec.size() == 5);
            CHECK(vec[0] == 1);
            CHECK(vec[1] == 2);
            CHECK(vec[2] == 3); // The new elements should be at indices 2 and 3
            CHECK(vec[3] == 4);
            CHECK(vec[4] == 5);
        }

        SECTION("at the end")
        {
            auto it = vec.insert(vec.end(), to_insert.begin(),
                                 to_insert.end()); // Insert 3 and 4 at the end
            CHECK(it ==
                  vec.begin() +
                      3); // The returned iterator should point to the first newly inserted element
            CHECK(vec.size() == 5);
            CHECK(vec[0] == 1);
            CHECK(vec[1] == 2);
            CHECK(vec[2] == 5);
            CHECK(vec[3] == 3); // The new elements should be at indices 3 and 4
            CHECK(vec[4] == 4);
        }
    }

    SECTION("adds elements from a strict single-pass input_iterator range via append-and-rotate")
    {
        int raw_data[] = {3, 4};
        MinimalInputIterator first{raw_data};
        MinimalInputIterator last{raw_data + 2};

        SECTION("inserts correctly in the middle of data using single-pass logic")
        {
            auto it = vec.insert(vec.begin() + 2, first, last);

            CHECK(it == vec.begin() + 2);
            REQUIRE(vec.size() == 5);
            CHECK(vec[0] == 1);
            CHECK(vec[1] == 2);
            CHECK(vec[2] == 3); // Rotated into position successfully
            CHECK(vec[3] == 4);
            CHECK(vec[4] == 5);
        }

        SECTION("inserts correctly at the beginning using single-pass logic")
        {
            auto it = vec.insert(vec.begin(), first, last);

            CHECK(it == vec.begin());
            REQUIRE(vec.size() == 5);
            CHECK(vec[0] == 3);
            CHECK(vec[1] == 4);
            CHECK(vec[2] == 1);
        }
    }

    SECTION("inserts an empty range without modifying the vector")
    {
        std::vector<int> empty_to_insert;
        auto it = vec.insert(vec.begin() + 2, empty_to_insert.begin(),
                             empty_to_insert.end()); // Insert an empty range at index 2
        CHECK(it == vec.begin() + 2); // Should return an iterator to the original position
        CHECK(vec.size() == 3);       // Size should remain unchanged
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 2);
        CHECK(vec[2] == 5);
    }

    SECTION("inserts a range of one element correctly")
    {
        std::vector<int> single_to_insert{3};
        auto it = vec.insert(vec.begin() + 2, single_to_insert.begin(),
                             single_to_insert.end()); // Insert a single element at index 2
        CHECK(it ==
              vec.begin() + 2); // The returned iterator should point to the newly inserted element
        CHECK(vec.size() == 4);
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 2);
        CHECK(vec[2] == 3); // The new element should be at index 2
        CHECK(vec[3] == 5);
    }

    SECTION("inserts a range correctly to an empty vector")
    {
        forge::vector<int> empty_vec;
        std::vector<int> to_insert{1, 2, 3};
        auto it = empty_vec.insert(empty_vec.begin(), to_insert.begin(),
                                   to_insert.end()); // Insert into an empty vector
        CHECK(it == empty_vec.begin()); // The returned iterator should point to the first newly
                                        // inserted element
        CHECK(empty_vec.size() == 3);
        CHECK(empty_vec[0] == 1);
        CHECK(empty_vec[1] == 2);
        CHECK(empty_vec[2] == 3);
    }

    SECTION("triggers a reallocation if the new size exceeds the current capacity")
    {
        forge::vector<int> vec(10, 0);
        SECTION("by 1.5x")
        {
            forge::vector<int> to_insert(2, 1);
            auto it =
                vec.insert(vec.end(), to_insert.begin(),
                           to_insert.end()); // This should trigger a reallocation since the new
                                             // size will exceed the initial capacity of 10
            CHECK(vec.capacity() ==
                  15); // After reallocation, the capacity should be 1.5x the previous capacity
            CHECK(it ==
                  vec.begin() +
                      10); // The returned iterator should point to the first newly inserted element
            for (std::size_t i = 0; i < 10; ++i)
            {
                CHECK(vec[i] == 0); // Existing elements should be preserved
            }
            for (std::size_t i = 10; i < vec.size(); ++i)
            {
                CHECK(vec[i] == 1); // New elements should be correctly inserted
            }
        }

        SECTION("by size() + count")
        {
            forge::vector<int> to_insert(20, 1);
            auto it =
                vec.insert(vec.end(), to_insert.begin(),
                           to_insert.end()); // This should trigger a reallocation since the new
                                             // size will exceed the initial capacity of 10
            CHECK(vec.capacity() ==
                  30); // After reallocation, the capacity should be size() + count
            CHECK(it ==
                  vec.begin() +
                      10); // The returned iterator should point to the first newly inserted element
            for (std::size_t i = 0; i < 10; ++i)
            {
                CHECK(vec[i] == 0); // Existing elements should be preserved
            }
            for (std::size_t i = 10; i < vec.size(); ++i)
            {
                CHECK(vec[i] == 1); // New elements should be correctly inserted
            }
        }
    }

    SECTION("does not trigger a reallocation if the new size does not exceed the current capacity")
    {
        forge::vector<int> vec(10, 0);
        vec.reserve(20);
        forge::vector<int> to_insert{1, 2, 3};
        auto it = vec.insert(vec.begin(), to_insert.begin(),
                             to_insert.end()); // Insert three elements at the beginning
        CHECK(vec.capacity() == 20);           // Capacity should remain unchanged
        CHECK(
            it ==
            vec.begin()); // The returned iterator should point to the first newly inserted element
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 2);
        CHECK(vec[2] == 3);
        for (std::size_t i = 3; i < vec.size(); ++i)
        {
            CHECK(vec[i] == 0); // Existing elements should be preserved
        }
    }

    SECTION("handles allocation failures")
    {
        ToggleAllocator<int>::should_fail =
            false; // Ensure that the allocator is not set to fail before creating the vector
        forge::vector<int, ToggleAllocator<int>> vec(
            5, 1,
            ToggleAllocator<int>{}); // Create a vector with 5 elements using the ToggleAllocator
        std::vector<int> to_insert(10, 1);
        ToggleAllocator<int>::should_fail = true; // Set the allocator to fail on allocation
        REQUIRE_THROWS_AS(vec.insert(vec.end(), to_insert.begin(), to_insert.end()),
                          std::bad_alloc);

        // After a failed insertion, the vector should still be in a valid state with its original
        // size and capacity
        CHECK(vec.size() == 5);
        CHECK(vec.capacity() == 5);
        REQUIRE(vec.data() != nullptr);
        for (std::size_t i = 0; i < vec.size(); ++i)
        {
            CHECK(vec[i] == 1); // Existing elements should still be intact after allocation failure
        }
    }

    SECTION("honors the exception safety guarantees")
    {
        SECTION("when reallocation is needed")
        {
            ThrowsAfterNConstructions::reset(-1); // Disable throwing so we can create the vector
            forge::vector<ThrowsAfterNConstructions> vec(5, 1);
            std::vector<ThrowsAfterNConstructions> to_insert(10, 1);
            ThrowsAfterNConstructions::reset(
                2); // Set the constructor to throw after 2 constructions

            REQUIRE_THROWS_AS(vec.insert(vec.end(), to_insert.begin(), to_insert.end()),
                              std::runtime_error);
            CHECK(ThrowsAfterNConstructions::count ==
                  0); // All constructed elements should have been destroyed

            // After a failed insert, the vector should still be in a valid state with its original
            // size and capacity
            CHECK(vec.size() == 5);
            CHECK(vec.capacity() == 5);
        }

        SECTION("when no reallocation is needed")
        {
            ThrowsAfterNConstructions::reset(-1); // Disable throwing so we can create the vector
            forge::vector<ThrowsAfterNConstructions> vec(5);
            vec.reserve(20); // Ensure that no reallocation will be needed for the insert
            std::vector<ThrowsAfterNConstructions> to_insert(5);
            ThrowsAfterNConstructions::reset(
                2); // Set the constructor to throw after 2 constructions

            REQUIRE_THROWS_AS(vec.insert(vec.begin(), to_insert.begin(), to_insert.end()),
                              std::runtime_error);
            CHECK(ThrowsAfterNConstructions::count ==
                  0); // All constructed elements should have been destroyed

            // After a failed insert, the vector should still be in a valid state with its original
            // size and capacity
            CHECK(vec.size() == 5);
            CHECK(vec.capacity() == 20);
        }
    }
}

TEST_CASE("Vector insert(pos, initializer_list)", "[vector][modifiers]")
{
    // This overload of insert() is essentially a convenience wrapper around the range insert(pos,
    // first, last) that constructs the elements from the initializer list and then forwards to the
    // range insert. Therefore, we can test it by verifying that it correctly constructs the
    // elements from the initializer list and then relies on the correctness of the range insert
    // implementation for the actual insertion logic.

    SECTION("adds elements from the initializer list at the specified position in the vector, "
            "increasing its size by the number of elements in the list while preserving existing "
            "elements")
    {
        forge::vector<int> vec{1, 2, 5};

        SECTION("at the beginning")
        {
            auto it = vec.insert(vec.begin(), {3, 4}); // Insert 3 and 4 at the beginning
            CHECK(it == vec.begin()); // The returned iterator should point to the first newly
                                      // inserted element
            CHECK(vec.size() == 5);
            CHECK(vec[0] == 3); // The new elements should be at indices 0 and 1
            CHECK(vec[1] == 4);
            CHECK(vec[2] == 1);
            CHECK(vec[3] == 2);
            CHECK(vec[4] == 5);
        }

        SECTION("in the middle")
        {
            auto it = vec.insert(vec.begin() + 2, {3, 4}); // Insert 3 and 4 at index 2
            CHECK(it ==
                  vec.begin() +
                      2); // The returned iterator should point to the first newly inserted element
            CHECK(vec.size() == 5);
            CHECK(vec[0] == 1);
            CHECK(vec[1] == 2);
            CHECK(vec[2] == 3); // The new elements should be at indices 2 and 3
            CHECK(vec[3] == 4);
            CHECK(vec[4] == 5);
        }

        SECTION("at the end")
        {
            auto it = vec.insert(vec.end(), {3, 4}); // Insert 3 and 4 at the end
            CHECK(it ==
                  vec.begin() +
                      3); // The returned iterator should point to the first newly inserted element
            CHECK(vec.size() == 5);
            CHECK(vec[0] == 1);
            CHECK(vec[1] == 2);
            CHECK(vec[2] == 5);
            CHECK(vec[3] == 3); // The new elements should be at indices 3 and 4
            CHECK(vec[4] == 4);
        }
    }
}

TEST_CASE("Vector emplace()", "[vector][modifiers]")
{
    SECTION("adds an element at the specified position in the vector, increasing its size by one "
            "while preserving existing elements")
    {
        SECTION("at the beginning")
        {
            forge::vector<int> vec{2, 3};
            auto it = vec.emplace(vec.begin(), 1); // Emplace 1 at the beginning
            CHECK(it ==
                  vec.begin()); // The returned iterator should point to the newly emplaced element
            CHECK(vec.size() == 3);
            CHECK(vec[0] == 1);
            CHECK(vec[1] == 2);
            CHECK(vec[2] == 3);
        }

        SECTION("in the middle")
        {
            forge::vector<int> vec{1, 3};
            auto it = vec.emplace(vec.begin() + 1, 2); // Emplace 2 in the middle
            CHECK(it == vec.begin() +
                            1); // The returned iterator should point to the newly emplaced element
            CHECK(vec.size() == 3);
            CHECK(vec[0] == 1);
            CHECK(vec[1] == 2);
            CHECK(vec[2] == 3);
        }

        SECTION("at the end")
        {
            forge::vector<int> vec{1, 2};
            auto it = vec.emplace(vec.end(), 3); // Emplace 3 at the end
            CHECK(it == vec.begin() +
                            2); // The returned iterator should point to the newly emplaced element
            CHECK(vec.size() == 3);
            CHECK(vec[0] == 1);
            CHECK(vec[1] == 2);
            CHECK(vec[2] == 3);
        }
    }

    SECTION("perfectly forwards arguments to construct the new element in place")
    {
        forge::vector<std::pair<int, std::string>> vec;
        auto it = vec.emplace(
            vec.begin(), 42,
            "Hello"); // Should perfectly forward the arguments to construct the pair in place
        CHECK(vec.size() == 1);
        CHECK(it->first == 42);
        CHECK(it->second == "Hello");
    }

    SECTION("constructs elements in place")
    {
        forge::vector<std::string> vec;
        auto it = vec.emplace(vec.begin(), 5,
                              'a'); // Should construct a string of 5 'a' characters in place
        CHECK(vec.size() == 1);
        CHECK(*it == "aaaaa");
    }

    SECTION("triggers a reallocation of 1.5x if the size exceeds the current capacity")
    {
        forge::vector<int> vec(10, 0);
        auto it = vec.emplace(vec.end(), 1); // This should trigger a reallocation since the size
                                             // will exceed the initial capacity of 10
        CHECK(vec.capacity() ==
              15); // After reallocation, the capacity should be at 1.5x the previous capacity
        CHECK(it ==
              vec.begin() + 10); // The returned iterator should point to the newly emplaced element
        for (std::size_t i = 0; i < 10; ++i)
        {
            CHECK(vec[i] == 0); // Existing elements should be preserved
        }
        CHECK(vec[10] == 1); // New element should be correctly emplaced
    }

    SECTION("does not trigger a reallocation if the size does not exceed the current capacity")
    {
        forge::vector<int> vec;
        vec.reserve(10); // Reserve capacity for 10 elements
        auto it = vec.emplace(vec.end(), 1);
        CHECK(vec.capacity() == 10);
        CHECK(it ==
              vec.begin()); // The returned iterator should point to the newly emplaced element
        CHECK(vec[0] == 1); // New element should be correctly emplaced
    }

    SECTION("handles allocation failures")
    {
        ToggleAllocator<int>::should_fail =
            false; // Ensure that the allocator is not set to fail before creating the vector
        forge::vector<int, ToggleAllocator<int>> vec(
            5, 1,
            ToggleAllocator<int>{}); // Create a vector with 5 elements using the ToggleAllocator
        ToggleAllocator<int>::should_fail = true; // Set the allocator to fail on allocation
        REQUIRE_THROWS_AS(vec.emplace(vec.end(), 1), std::bad_alloc);
    }

    SECTION("honors the exception safety guarantees")
    {
        forge::vector<ThrowsAfterNConstructions> vec(5);

        SECTION("when reallocation is needed")
        {
            ThrowsAfterNConstructions::reset(-1); // Disable throwing so we can create the vector
            ThrowsAfterNConstructions::reset(0);  // Set the constructor to throw immediately

            REQUIRE_THROWS_AS(vec.emplace(vec.end(), ThrowsAfterNConstructions()),
                              std::runtime_error);
            CHECK(ThrowsAfterNConstructions::count ==
                  0); // All constructed elements should have been destroyed

            // After a failed emplace, the vector should still be in a valid state with its original
            // size and capacity
            CHECK(vec.size() == 5);
            CHECK(vec.capacity() == 5);
        }

        SECTION("when no reallocation is needed")
        {
            ThrowsAfterNConstructions::reset(-1); // Disable throwing so we can create the vector
            vec.reserve(20); // Ensure that no reallocation will be needed for the emplace
            ThrowsAfterNConstructions::reset(0); // Set the constructor to throw immediately

            REQUIRE_THROWS_AS(vec.emplace(vec.begin(), ThrowsAfterNConstructions()),
                              std::runtime_error);
            CHECK(ThrowsAfterNConstructions::count ==
                  0); // All constructed elements should have been destroyed

            // After a failed emplace, the vector should still be in a valid state with its original
            // size
            CHECK(vec.size() == 5);
            CHECK(vec.capacity() == 20);
        }
    }
}

TEST_CASE("Vector pop_back()", "[vector][modifiers]")
{
    forge::vector<int> vec{1, 2, 3};

    SECTION("removes the last element from the vector, reducing its size by one while preserving "
            "existing elements")
    {
        vec.pop_back();
        CHECK(vec.size() == 2);
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 2);
    }

    SECTION("does not modify the capacity of the vector")
    {
        vec.reserve(10); // Reserve capacity for 10 elements
        vec.pop_back();
        CHECK(vec.capacity() == 10); // Capacity should still remain unchanged after pop_back
    }

    SECTION("does nothing if the vector is empty")
    {
        forge::vector<int> empty_vec;
        empty_vec.pop_back();         // Should not do anything since the vector is already empty
        CHECK(empty_vec.size() == 0); // Size should remain unchanged
        CHECK(empty_vec.capacity() == 0); // Capacity should remain unchanged
    }

    SECTION("honors the noexcept guarantee")
    {
        CHECK(noexcept(vec.pop_back())); // pop_back should be noexcept
    }
}

TEST_CASE("Vector erase(pos)", "[vector][modifiers]")
{
    forge::vector<int> vec{1, 2, 3};

    SECTION("removes the element at the specified position in the vector, reducing its size by "
            "one while preserving existing elements")
    {
        auto it = vec.erase(vec.begin() + 1); // Erase the element at index 1 (value 2)
        CHECK(it == vec.begin() + 1); // The returned iterator should point to the element that
                                      // followed the erased element (value 3)
        CHECK(vec.size() == 2);
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 3);
    }

    SECTION("removes the first element when erasing the element at the beginning of the vector")
    {
        auto it = vec.erase(vec.begin()); // Erase the first element (value 1)
        CHECK(it == vec.begin()); // The returned iterator should point to the new first element
                                  // (value 2)
        CHECK(vec.size() == 2);
        CHECK(vec[0] == 2);
        CHECK(vec[1] == 3);
    }

    SECTION("returns end() if the erased element was the last element in the vector")
    {
        auto it = vec.erase(vec.begin() + 2); // Erase the last element (value 3)
        CHECK(it == vec.end()); // The returned iterator should be end() since there are no elements
                                // following the erased element
        CHECK(vec.size() == 2);
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 2);
    }

    SECTION("does not modify the capacity of the vector")
    {
        vec.reserve(10);             // Reserve capacity for 10 elements
        vec.erase(vec.begin() + 1);  // Erase an element from the vector
        CHECK(vec.capacity() == 10); // Capacity should still remain unchanged after erase
    }

    SECTION("honors the conditional noexcept guarantee")
    {
        forge::vector<int> trivial_vec;
        forge::vector<ThrowingMove> throwing_vec;

        CHECK(noexcept(trivial_vec.erase(trivial_vec.begin())));
        CHECK_FALSE(noexcept(throwing_vec.erase(throwing_vec.begin())));
    }
}

TEST_CASE("Vector erase(first, last)", "[vector][modifiers]")
{
    forge::vector<int> vec{1, 2, 3, 4, 5};

    SECTION("erasing a sub-range from the middle shifts remaining elements correctly")
    {
        // Target the range containing {3, 4}
        auto first = vec.begin() + 2; // Points to 3
        auto last = vec.begin() + 4;  // Points to 5 (exclusive boundary)

        auto ret = vec.erase(first, last);

        REQUIRE(vec.size() == 3);
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 2);
        CHECK(vec[2] == 5); // 50 shifted left by 2 slots
        CHECK(*ret == 5);   // Return iterator points to the first element after the erased chunk
    }

    SECTION("erasing from begin() to end() empties the vector but preserves capacity")
    {
        vec.reserve(20);

        auto ret = vec.erase(vec.begin(), vec.end());

        CHECK(vec.size() == 0);
        CHECK(vec.capacity() == 20);
        CHECK(ret == vec.end());
    }

    SECTION("passing an empty range (first == last) is a safe no-op")
    {
        auto target = vec.begin() + 1; // Points to 2

        auto ret = vec.erase(target, target);

        REQUIRE(vec.size() == 5);
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 2);
        CHECK(vec[2] == 3);
        CHECK(ret == target); // Returns the unchanged iterator
    }

    SECTION("erasing a prefix shifts the remaining tail elements to the front")
    {
        auto ret = vec.erase(vec.begin(), vec.begin() + 3); // Erases {1, 2, 3}

        REQUIRE(vec.size() == 2);
        CHECK(vec[0] == 4);
        CHECK(vec[1] == 5);
        CHECK(ret == vec.begin());
    }

    SECTION("erasing a suffix removes trailing elements and returns end()")
    {
        auto ret = vec.erase(vec.begin() + 3, vec.end()); // Erases {4, 5}

        REQUIRE(vec.size() == 3);
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 2);
        CHECK(vec[2] == 3);
        CHECK(ret == vec.end());
    }

    SECTION("honors the conditional noexcept guarantee")
    {
        forge::vector<int> trivial_vec;
        forge::vector<ThrowingMove> throwing_vec;

        CHECK(noexcept(trivial_vec.erase(trivial_vec.begin(), trivial_vec.end())));
        CHECK_FALSE(noexcept(throwing_vec.erase(throwing_vec.begin(), throwing_vec.end())));
    }
}

TEST_CASE("Vector resize(count)", "[vector][modifiers]")
{
    forge::vector<int> vec{1, 2, 3};

    SECTION("resizes the vector to contain count elements")
    {
        vec.resize(5);
        CHECK(vec.size() == 5);
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 2);
        CHECK(vec[2] == 3);
    }

    SECTION("default constructs new elements if count is greater than the current size")
    {
        forge::vector<Tracked> vec;
        Tracked::reset(); // Reset the Tracked class state to ensure a clean slate for testing
        vec.resize(3);    // Resize to 3 elements, which should default construct 3 Tracked objects
        CHECK(vec.size() == 3);
        CHECK(Tracked::default_constructed == 3); // Should have called the default constructor
    }

    SECTION("truncates the vector if count is less than the current size")
    {
        vec.resize(2);
        CHECK(vec.size() == 2);
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 2);
        CHECK(vec.capacity() ==
              3); // Capacity should still remain unchanged after resizing to a smaller size
    }

    SECTION("triggers a reallocation if count exceeds the current capacity")
    {
        vec.resize(15); // This should trigger a reallocation since the new size will exceed the
                        // initial capacity of 3
        CHECK(vec.capacity() == 15); // After reallocation, the capacity should be the new size
        for (std::size_t i = 0; i < 3; ++i)
        {
            CHECK(vec[i] == i + 1); // Existing elements should be preserved
        }
    }

    SECTION("does not trigger a reallocation if count does not exceed the current capacity")
    {
        forge::vector<int> vec;
        vec.reserve(10); // Reserve capacity for 10 elements
        vec.resize(5);
        CHECK(vec.capacity() ==
              10); // Capacity should still remain unchanged after resizing within capacity
    }

    SECTION("throws std::length_error if count exceeds max_size()")
    {
        REQUIRE_THROWS_AS(vec.resize(vec.max_size() + 1), std::length_error);
    }

    SECTION("handles allocation failures")
    {
        ToggleAllocator<int>::should_fail =
            false; // Ensure that the allocator is not set to fail before creating the vector
        forge::vector<int, ToggleAllocator<int>> vec(
            5, 1,
            ToggleAllocator<int>{}); // Create a vector with 5 elements using the ToggleAllocator
        ToggleAllocator<int>::should_fail = true; // Set the allocator to fail on allocation
        REQUIRE_THROWS_AS(vec.resize(10), std::bad_alloc);

        // After a failed resize, the vector should still be in a valid state with its original size
        // and capacity
        CHECK(vec.size() == 5);
        CHECK(vec.capacity() == 5);
        REQUIRE(vec.data() != nullptr);
        for (std::size_t i = 0; i < vec.size(); ++i)
        {
            CHECK(vec[i] == 1); // Existing elements should still be intact after allocation failure
        }
    }

    SECTION("honors the exception safety guarantees")
    {
        ThrowsAfterNConstructions::reset(-1); // Disable throwing so we can create the vector
        forge::vector<ThrowsAfterNConstructions> vec(5);

        SECTION("when increasing size with reallocation")
        {
            ThrowsAfterNConstructions::reset(
                3); // Set the constructor to throw after 3 constructions

            REQUIRE_THROWS_AS(vec.resize(10), std::runtime_error);
            CHECK(ThrowsAfterNConstructions::count ==
                  0); // All constructed elements should have been destroyed

            // After a failed resize, the vector should still be in a valid state with its original
            // size and capacity
            CHECK(vec.size() == 5);
            CHECK(vec.capacity() == 5);
        }

        SECTION("when increasing size without reallocation")
        {
            vec.reserve(20); // Ensure that no reallocation will be needed for the resize
            ThrowsAfterNConstructions::reset(
                3); // Set the constructor to throw after 3 constructions

            REQUIRE_THROWS_AS(vec.resize(10), std::runtime_error);
            CHECK(ThrowsAfterNConstructions::count ==
                  0); // All constructed elements should have been destroyed

            // After a failed resize, the vector should still be in a valid state with its original
            // size and capacity
            CHECK(vec.size() == 5);
            CHECK(vec.capacity() == 20);
        }
    }
}

TEST_CASE("Vector resize(count, value)", "[vector][modifiers]")
{
    forge::vector<int> vec{1, 2, 3};

    SECTION("resizes the vector to contain count elements, initializing new elements with the "
            "specified value")
    {
        vec.resize(5, 42);
        CHECK(vec.size() == 5);
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 2);
        CHECK(vec[2] == 3);
        CHECK(vec[3] == 42); // New elements should be initialized to the specified value
        CHECK(vec[4] == 42);
    }

    SECTION("truncates the vector if count is less than the current size")
    {
        vec.resize(2, 42); // The value parameter should be ignored when resizing to a smaller size
        CHECK(vec.size() == 2);
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 2);
        CHECK(vec.capacity() ==
              3); // Capacity should still remain unchanged after resizing to a smaller size
    }

    SECTION("triggers a reallocation if count exceeds the current capacity")
    {
        vec.resize(15, 42); // This should trigger a reallocation since the new size will exceed the
                            // initial capacity of 3
        CHECK(vec.capacity() == 15); // After reallocation, the capacity should be the new size
        for (std::size_t i = 0; i < 3; ++i)
        {
            CHECK(vec[i] == i + 1); // Existing elements should be preserved
        }
        for (std::size_t i = 3; i < vec.size(); ++i)
        {
            CHECK(vec[i] == 42); // New elements should be initialized to the specified value
        }
    }

    SECTION("does not trigger a reallocation if count does not exceed the current capacity")
    {
        forge::vector<int> vec;
        vec.reserve(10); // Reserve capacity for 10 elements
        vec.resize(5, 42);
        CHECK(vec.capacity() == 10); // Capacity should remain unchanged
        for (std::size_t i = 0; i < vec.size(); ++i)
        {
            CHECK(vec[i] == 42); // New elements should be initialized to the specified value
        }
    }

    SECTION("throws std::length_error if count exceeds max_size()")
    {
        REQUIRE_THROWS_AS(vec.resize(vec.max_size() + 1, 42), std::length_error);
    }

    SECTION("handles allocation failures")
    {
        ToggleAllocator<int>::should_fail =
            false; // Ensure that the allocator is not set to fail before creating the vector
        forge::vector<int, ToggleAllocator<int>> vec(
            5, 1,
            ToggleAllocator<int>{}); // Create a vector with 5 elements using the ToggleAllocator
        ToggleAllocator<int>::should_fail = true; // Set the allocator to fail on allocation
        REQUIRE_THROWS_AS(vec.resize(10, 42), std::bad_alloc);

        // After a failed resize, the vector should still be in a valid state with its original size
        // and capacity
        CHECK(vec.size() == 5);
        CHECK(vec.capacity() == 5);
        REQUIRE(vec.data() != nullptr);
        for (std::size_t i = 0; i < vec.size(); ++i)
        {
            CHECK(vec[i] == 1); // Existing elements should still be intact after allocation failure
        }
    }

    SECTION("honors the exception safety guarantees")
    {
        ThrowsAfterNConstructions::reset(-1); // Disable throwing so we can create the vector
        forge::vector<ThrowsAfterNConstructions> vec(5);

        SECTION("when increasing size with reallocation")
        {
            ThrowsAfterNConstructions::reset(
                3); // Set the constructor to throw after 3 constructions

            REQUIRE_THROWS_AS(vec.resize(10, ThrowsAfterNConstructions()), std::runtime_error);
            CHECK(ThrowsAfterNConstructions::count ==
                  0); // All constructed elements should have been destroyed

            // After a failed resize, the vector should still be in a valid state with its original
            // size and capacity
            CHECK(vec.size() == 5);
            CHECK(vec.capacity() == 5);
        }

        SECTION("when increasing size without reallocation")
        {
            vec.reserve(20); // Ensure that no reallocation will be needed for the resize
            ThrowsAfterNConstructions::reset(
                3); // Set the constructor to throw after 3 constructions

            REQUIRE_THROWS_AS(vec.resize(10, ThrowsAfterNConstructions()), std::runtime_error);
            CHECK(ThrowsAfterNConstructions::count ==
                  0); // All constructed elements should have been destroyed

            // After a failed resize, the vector should still be in a valid state with its original
            // size and capacity
            CHECK(vec.size() == 5);
            CHECK(vec.capacity() == 20);
        }
    }
}

TEST_CASE("Vector clear()", "[vector][modifiers]")
{
    forge::vector<int> vec{1, 2, 3};

    SECTION("removes all elements from the vector, reducing its size to zero while preserving "
            "capacity")
    {
        vec.clear();
        CHECK(vec.size() == 0);
        CHECK(vec.capacity() == 3); // Capacity should remain unchanged after clear
    }

    SECTION("does nothing if the vector is already empty")
    {
        forge::vector<int> empty_vec;
        empty_vec.clear();            // Should not do anything since the vector is already empty
        CHECK(empty_vec.size() == 0); // Size should remain unchanged
        CHECK(empty_vec.capacity() == 0); // Capacity should remain unchanged
    }

    SECTION("honors the noexcept guarantee")
    {
        CHECK(noexcept(vec.clear())); // clear should be noexcept
    }
}

TEST_CASE("Vector assign()", "[vector][modifiers]")
{
    forge::vector<int> vec{1, 2, 3};

    SECTION("with count and value parameters")
    {
        SECTION("assigns count copies of value to the vector, replacing its current contents and "
                "changing "
                "its size to count")
        {
            vec.assign(5, 42); // Assign 5 copies of 42 to the vector
            CHECK(vec.size() == 5);
            CHECK(vec.capacity() ==
                  5); // Capacity should be updated to the new size after assignment
            for (std::size_t i = 0; i < vec.size(); ++i)
            {
                CHECK(vec[i] == 42); // All elements should be assigned the value 42
            }
        }

        SECTION("assigns zero copies of value when count is zero, resulting in an empty vector")
        {
            vec.assign(0, 42);            // Assign zero copies of 42 to the vector
            CHECK(vec.size() == 0);       // Size should be zero after assignment
            CHECK(vec.capacity() == 0);   // Capacity should be updated to zero after assignment
            CHECK(vec.data() == nullptr); // Data pointer should be null for an empty vector
        }

        SECTION("works for empty vectors")
        {
            forge::vector<int> empty_vec;
            empty_vec.assign(5, 42); // Assign 5 copies of 42 to an empty vector
            CHECK(empty_vec.size() == 5);
            CHECK(empty_vec.capacity() ==
                  5); // Capacity should be updated to the new size after assignment
            for (std::size_t i = 0; i < empty_vec.size(); ++i)
            {
                CHECK(empty_vec[i] == 42); // All elements should be assigned the value 42
            }
        }

        SECTION("handles allocation failures")
        {
            ToggleAllocator<int>::should_fail =
                false; // Ensure that the allocator is not set to fail before creating the vector
            forge::vector<int, ToggleAllocator<int>> vec(
                5, 1, ToggleAllocator<int>{});        // Create a vector with 5 elements using the
                                                      // ToggleAllocator
            ToggleAllocator<int>::should_fail = true; // Set the allocator to fail on allocation
            REQUIRE_THROWS_AS(vec.assign(10, 42), std::bad_alloc);

            // After a failed assignment, the vector should still be in a valid state with its
            // original size and capacity
            CHECK(vec.size() == 5);
            CHECK(vec.capacity() == 5);
            REQUIRE(vec.data() != nullptr);
            for (std::size_t i = 0; i < vec.size(); ++i)
            {
                CHECK(vec[i] ==
                      1); // Existing elements should still be intact after allocation failure
            }
        }

        SECTION("honors the exception safety guarantees")
        {
            ThrowsAfterNConstructions::reset(-1); // Disable throwing so we can create the vector
            forge::vector<ThrowsAfterNConstructions> vec(5);
            ThrowsAfterNConstructions::reset(
                3); // Set the constructor to throw after 3 constructions

            REQUIRE_THROWS_AS(vec.assign(10, ThrowsAfterNConstructions()), std::runtime_error);
            CHECK(ThrowsAfterNConstructions::count ==
                  0); // All constructed elements should have been destroyed

            // After a failed assignment, the vector should still be in a valid state with its
            // original size and capacity
            CHECK(vec.size() == 5);
            CHECK(vec.capacity() == 5);
        }
    }

    SECTION("with range parameters")
    {
        SECTION("assigns elements from the specified range, replacing the vector's current "
                "contents and changing its size to match the number of elements in the range")
        {
            std::vector<int> new_values{4, 5, 6};
            vec.assign(new_values.begin(), new_values.end()); // Assign new values from a range
            CHECK(vec.size() == 3);
            CHECK(vec.capacity() == 3);
            CHECK(vec[0] == 4);
            CHECK(vec[1] == 5);
            CHECK(vec[2] == 6);
        }

        SECTION("assignment from an empty range, results in an empty vector")
        {
            std::vector<int> empty_values;
            vec.assign(empty_values.begin(), empty_values.end()); // Assign an empty range
            CHECK(vec.size() == 0);       // Size should be zero after assignment
            CHECK(vec.capacity() == 0);   // Capacity should be updated to zero after assignment
            CHECK(vec.data() == nullptr); // Data pointer should be null for an empty vector
        }

        SECTION("works for empty vectors")
        {
            forge::vector<int> empty_vec;
            std::vector<int> new_values{1, 2, 3};
            empty_vec.assign(new_values.begin(),
                             new_values.end()); // Assign new values to an empty vector
            CHECK(empty_vec.size() == 3);
            CHECK(empty_vec.capacity() == 3);
            CHECK(empty_vec[0] == 1);
            CHECK(empty_vec[1] == 2);
            CHECK(empty_vec[2] == 3);
        }

        SECTION("handles allocation failures")
        {
            ToggleAllocator<int>::should_fail =
                false; // Ensure that the allocator is not set to fail before creating the vector
            forge::vector<int, ToggleAllocator<int>> vec(
                5, 1, ToggleAllocator<int>{}); // Create a vector with 5 elements using the
                                               // ToggleAllocator
            std::vector<int> new_values(10, 42);
            ToggleAllocator<int>::should_fail = true; // Set the allocator to fail on allocation
            REQUIRE_THROWS_AS(vec.assign(new_values.begin(), new_values.end()), std::bad_alloc);

            // After a failed assignment, the vector should still be in a valid state with its
            // original size and capacity
            CHECK(vec.size() == 5);
            CHECK(vec.capacity() == 5);
            REQUIRE(vec.data() != nullptr);
            for (std::size_t i = 0; i < vec.size(); ++i)
            {
                CHECK(vec[i] ==
                      1); // Existing elements should still be intact after allocation failure
            }
        }

        SECTION("honors the exception safety guarantees")
        {
            ThrowsAfterNConstructions::reset(-1); // Disable throwing so we can create the vector
            forge::vector<ThrowsAfterNConstructions> vec(5);
            std::vector<ThrowsAfterNConstructions> new_values(10);
            ThrowsAfterNConstructions::reset(
                3); // Set the constructor to throw after 3 constructions

            REQUIRE_THROWS_AS(vec.assign(new_values.begin(), new_values.end()), std::runtime_error);
            CHECK(ThrowsAfterNConstructions::count ==
                  0); // All constructed elements should have been destroyed

            // After a failed assignment, the vector should still be in a valid state with its
            // original size and capacity
            CHECK(vec.size() == 5);
            CHECK(vec.capacity() == 5);
        }
    }

    SECTION("with initializer list parameter")
    {
        // This overload of assign() is essentially a convenience wrapper around the range
        // assign(first, last) that constructs the elements from the initializer list and then
        // forwards to the range assign. Therefore, we can test it by verifying that it correctly
        // constructs the elements from the initializer list and then relies on the correctness of
        // the range assign implementation for the actual assignment logic.

        vec.assign({4, 5, 6}); // Assign new values from an initializer
        CHECK(vec.size() == 3);
        CHECK(vec[0] == 4);
        CHECK(vec[1] == 5);
        CHECK(vec[2] == 6);
    }
}

TEST_CASE("Vector swap()", "[vector][modifiers]")
{
    forge::vector<int> vec1{1, 2, 3};
    forge::vector<int> vec2{4, 5};

    SECTION("swaps the contents of two vectors, including their size and capacity")
    {
        vec1.swap(vec2);
        CHECK(vec1.size() == 2);
        CHECK(vec1.capacity() == 2);
        CHECK(vec1[0] == 4);
        CHECK(vec1[1] == 5);

        CHECK(vec2.size() == 3);
        CHECK(vec2.capacity() == 3);
        CHECK(vec2[0] == 1);
        CHECK(vec2[1] == 2);
        CHECK(vec2[2] == 3);
    }

    SECTION("swapping a vector with itself does not change its contents")
    {
        vec1.swap(vec1); // Swap vec1 with itself
        CHECK(vec1.size() == 3);
        CHECK(vec1.capacity() == 3);
        CHECK(vec1[0] == 1);
        CHECK(vec1[1] == 2);
        CHECK(vec1[2] == 3);
    }

    SECTION("honors the noexcept guarantee")
    {
        CHECK(noexcept(vec1.swap(vec2))); // swap should be noexcept
    }
}

TEST_CASE("Vector iterator default constructor", "[vector][iterators]")
{
    SECTION("creates nullptr internally")
    {
        forge::vector<int>::iterator it;
        forge::vector<int>::const_iterator cit;

        CHECK(it == nullptr);
        CHECK(cit == nullptr);
    }

    SECTION("honors the noexcept guarantee")
    {
        CHECK(noexcept(forge::vector<int>::iterator()));
        CHECK(noexcept(forge::vector<int>::const_iterator()));
    }
}

TEST_CASE("Vector iterator pointer constructor", "[vector][iterators]")
{
    forge::vector<int> vec = {10, 20, 30, 40};

    SECTION("constructs an iterator from a pointer to an element in the vector")
    {
        auto it1 = forge::vector<int>::iterator(
            vec.data()); // Constructing an iterator from a pointer to the first element
        auto it2 = forge::vector<int>::iterator(
            vec.data() + 2); // Constructing an iterator from a pointer to the third element
        auto it_end = forge::vector<int>::iterator(
            vec.data() +
            vec.size()); // Constructing an iterator from a pointer to one past the last element
        auto cit1 = forge::vector<int>::const_iterator(vec.data());     // Same for const
        auto cit2 = forge::vector<int>::const_iterator(vec.data() + 2); // Same for const
        auto cit_end =
            forge::vector<int>::const_iterator(vec.data() + vec.size()); // Same for const

        CHECK(*it1 == 10);
        CHECK(*it2 == 30);
        CHECK(it_end == vec.end());
        CHECK(*cit1 == 10);
        CHECK(*cit2 == 30);
        CHECK(cit_end == vec.cend());
    }

    SECTION("accepts nullptr")
    {
        forge::vector<int>::iterator it(nullptr);
        forge::vector<int>::const_iterator cit(nullptr);

        CHECK(it == nullptr);
        CHECK(cit == nullptr);
    }

    SECTION("honors the noexcept guarantee")
    {
        CHECK(noexcept(forge::vector<int>::iterator(nullptr)));
        CHECK(noexcept(forge::vector<int>::const_iterator(nullptr)));
    }
}

TEST_CASE("Vector iterator converting constructor", "[vector][iterators]")
{
    forge::vector<int> vec = {1, 2, 3, 4, 5};

    SECTION("constructs a const_iterator from a non-const iterator")
    {
        forge::vector<int>::iterator it =
            vec.begin() + 2; // Create a non-const iterator pointing to the third element (value 3)
        forge::vector<int>::const_iterator cit =
            it; // Constructing a const_iterator from a non-const iterator

        CHECK(
            cit ==
            vec.cbegin() +
                2); // The const_iterator should point to the same element as the original iterator
        CHECK(*cit == 3);
    }

    SECTION("works in function parameters")
    {
        auto test_func = [](forge::vector<int>::const_iterator cit) { return *cit; };

        CHECK(test_func(vec.begin() + 1) ==
              2); // Passing a non-const iterator to a function that expects a const_iterator should
                  // work due to the converting constructor
    }

    SECTION("honors the noexcept guarantee")
    {
        forge::vector<int>::iterator it = vec.begin();
        CHECK(noexcept(
            forge::vector<int>::const_iterator(it))); // Converting constructor should be noexcept
    }
}

TEST_CASE("Vector iterator operator* dereference", "[vector][iterators]")
{
    forge::vector<std::string> vec = {"zero", "one", "two", "three"};

    SECTION("allows basic dereferencing to access the element pointed to by the iterator")
    {
        auto it = vec.begin();
        auto cit = vec.cbegin();
        CHECK(*it == "zero");
        CHECK(*cit == "zero");

        ++it, ++cit;
        CHECK(*it == "one");
        CHECK(*cit == "one");

        it = vec.end() - 1;
        cit = vec.cend() - 1;
        CHECK(*it == "three");
        CHECK(*cit == "three");
    }

    SECTION("allows mutable access through non-const iterator")
    {
        auto it = vec.begin();
        *it = "modified"; // Should be able to modify the element through a non-const iterator
        CHECK(vec[0] == "modified");
    }

    SECTION("honors the noexcept guarantee")
    {
        auto it = vec.begin();
        auto cit = vec.cbegin();
        CHECK(noexcept(*it));  // Dereferencing a non-const iterator should be noexcept
        CHECK(noexcept(*cit)); // Dereferencing a const iterator should be noexcept
    }
}

TEST_CASE("Vector iterator operator->", "[vector][iterators]")
{
    forge::vector<std::pair<int, std::string>> vec = {{1, "one"}, {2, "two"}, {3, "three"}};

    SECTION("allows access to members of the element pointed to by the iterator")
    {
        auto it = vec.begin();
        auto cit = vec.cbegin();
        CHECK(it->first == 1);
        CHECK(it->second == "one");
        CHECK(cit->first == 1);
        CHECK(cit->second == "one");

        ++it, ++cit;
        CHECK(it->first == 2);
        CHECK(it->second == "two");
        CHECK(cit->first == 2);
        CHECK(cit->second == "two");
    }

    SECTION("allows mutable access through non-const iterator")
    {
        auto it = vec.begin();
        it->second =
            "modified"; // Should be able to modify the element through a non-const iterator
        CHECK(vec[0].second == "modified");
    }

    SECTION("honors the noexcept guarantee")
    {
        auto it = vec.begin();
        auto cit = vec.cbegin();
        CHECK(noexcept(
            it->first)); // Accessing members through a non-const iterator should be noexcept
        CHECK(
            noexcept(cit->first)); // Accessing members through a const iterator should be noexcept
    }
}

TEST_CASE("Vector iterator operator[]", "[vector][iterators]")
{
    forge::vector<int> vec = {10, 20, 30, 40};

    SECTION("allows random access to elements at a specific offset from the iterator")
    {
        auto it = vec.begin();
        auto cit = vec.cbegin();
        CHECK(it[0] == 10);
        CHECK(it[1] == 20);
        CHECK(it[2] == 30);
        CHECK(it[3] == 40);
        CHECK(cit[0] == 10);
        CHECK(cit[1] == 20);
        CHECK(cit[2] == 30);
        CHECK(cit[3] == 40);
    }

    SECTION("allows negative offsets from the iterator")
    {
        auto it = vec.end();
        auto cit = vec.cend();
        CHECK(it[-1] == 40);
        CHECK(it[-2] == 30);
        CHECK(it[-3] == 20);
        CHECK(it[-4] == 10);
        CHECK(cit[-1] == 40);
        CHECK(cit[-2] == 30);
        CHECK(cit[-3] == 20);
        CHECK(cit[-4] == 10);
    }

    SECTION("allows mutable access through non-const iterator")
    {
        auto it = vec.begin();
        it[1] = 42; // Should be able to modify the element through a non-const iterator
        CHECK(vec[1] == 42);
    }

    SECTION("honors the noexcept guarantee")
    {
        auto it = vec.begin();
        auto cit = vec.cbegin();
        CHECK(
            noexcept(it[0])); // Accessing elements through a non-const iterator should be noexcept
        CHECK(noexcept(cit[0])); // Accessing elements through a const iterator should be noexcept
    }
}

TEST_CASE("Vector iterator increment operators", "[vector][iterators]")
{
    forge::vector<int> vec = {10, 20, 30, 40};

    SECTION("pre-increment operator (++it) advances the iterator to the next element and returns a "
            "reference to the incremented iterator")
    {
        auto it = vec.begin();
        auto cit = vec.cbegin();

        CHECK(*++it ==
              20); // Pre-increment should advance the iterator and return the incremented iterator
        CHECK(*++cit == 20);
        CHECK(*it == 20);
        CHECK(*cit == 20);

        // Non-const iterator should allow modification
        *++it = 99; // Should be able to modify the element through a non-const iterator
        CHECK(vec[2] == 99);
    }

    SECTION("post-increment operator (it++) advances the iterator to the next element but returns "
            "a copy of the iterator before it was incremented")
    {
        auto it = vec.begin();
        auto cit = vec.cbegin();

        CHECK(*it++ ==
              10); // Post-increment should return the original iterator before incrementing
        CHECK(*cit++ == 10);
        CHECK(*it == 20); // After post-increment, the iterator should have advanced
        CHECK(*cit == 20);

        // Non-const iterator should allow modification
        *it++ = 99; // Should be able to modify the element through a non-const iterator
        CHECK(vec[1] == 99);
        CHECK(*it ==
              30); // After post-increment, the iterator should have advanced to the next element
    }

    SECTION("honors the noexcept guarantee")
    {
        auto it = vec.begin();
        auto cit = vec.cbegin();
        CHECK(noexcept(++it));  // Pre-increment should be noexcept
        CHECK(noexcept(++cit)); // Pre-increment for const iterator should be noexcept
        CHECK(noexcept(it++));  // Post-increment should be noexcept
        CHECK(noexcept(cit++)); // Post-increment for const iterator should be noexcept
    }
}

TEST_CASE("Vector iterator decrement operators", "[vector][iterators]")
{
    forge::vector<int> vec = {10, 20, 30, 40};

    SECTION("pre-decrement operator (--it) moves the iterator to the previous element and returns "
            "a reference to the decremented iterator")
    {
        auto it = vec.end();
        auto cit = vec.cend();

        CHECK(
            *--it ==
            40); // Pre-decrement should move the iterator back and return the decremented iterator
        CHECK(*--cit == 40);
        CHECK(*it == 40);
        CHECK(*cit == 40);

        // Non-const iterator should allow modification
        *--it = 99; // Should be able to modify the element through a non-const iterator
        CHECK(vec[2] == 99);
    }

    SECTION("post-decrement operator (it--) moves the iterator to the previous element but returns "
            "a copy of the iterator before it was decremented")
    {
        auto it = vec.end() - 1;
        auto cit = vec.cend() - 1;

        CHECK(*it-- ==
              40); // Post-decrement should return the original iterator before decrementing
        CHECK(*cit-- == 40);
        CHECK(*it == 30); // After post-decrement, the iterator should have moved back
        CHECK(*cit == 30);

        // Non-const iterator should allow modification
        *it-- = 99; // Should be able to modify the element through a non-const iterator
        CHECK(vec[2] == 99);
        CHECK(*it == 20); // After post-decrement, the iterator should have moved back to the
                          // previous element
    }

    SECTION("honors the noexcept guarantee")
    {
        auto it = vec.end();
        auto cit = vec.cend();
        CHECK(noexcept(--it));  // Pre-decrement should be noexcept
        CHECK(noexcept(--cit)); // Pre-decrement for const iterator should be noexcept
        CHECK(noexcept(it--));  // Post-decrement should be noexcept
        CHECK(noexcept(cit--)); // Post-decrement for const iterator should be noexcept
    }
}

TEST_CASE("Vector iterator arithmetic operators", "[vector][iterators]")
{
    forge::vector<int> vec = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    SECTION("operator += and -=")
    {
        auto it = vec.begin() + 2;
        auto cit = vec.cbegin() + 2;

        it += 3;
        cit += 3;
        CHECK(*it == 6);
        CHECK(*cit == 6);

        it -= 2;
        cit -= 2;
        CHECK(*it == 4);
        CHECK(*cit == 4);

        it += 0;
        cit += 0;
        CHECK(*it == 4);
        CHECK(*cit == 4);

        it -= 0;
        cit -= 0;
        CHECK(*it == 4);
        CHECK(*cit == 4);

        it += -2;
        cit += -2;
        CHECK(*it == 2);
        CHECK(*cit == 2);

        it -= -2;
        cit -= -2;
        CHECK(*it == 4);
        CHECK(*cit == 4);
    }

    SECTION("operator+ (iterator + offset and offset + iterator)")
    {
        auto it = vec.begin();
        auto cit = vec.cbegin();

        CHECK(*(it + 0) == 1);
        CHECK(*(cit + 0) == 1);
        CHECK(*(it + 5) == 6);
        CHECK(*(cit + 5) == 6);

        CHECK(*(0 + it) == 1);
        CHECK(*(0 + cit) == 1);
        CHECK(*(5 + it) == 6);
        CHECK(*(5 + cit) == 6);

        it = vec.end();
        cit = vec.cend();
        CHECK(*(it + -2) == 9); // Adding a negative offset
        CHECK(*(cit + -2) == 9);
        CHECK(*(-2 + it) == 9);
        CHECK(*(-2 + cit) == 9);
    }

    SECTION("operator- (iterator - offset)")
    {
        auto it = vec.end();
        auto cit = vec.cend();

        CHECK(*(it - 1) == 10);
        CHECK(*(cit - 1) == 10);
        CHECK(*(it - 5) == 6);
        CHECK(*(cit - 5) == 6);

        it = vec.begin();
        cit = vec.cbegin();
        CHECK(*(it - -2) == 3); // Subtracting a negative offset
        CHECK(*(cit - -2) == 3);
    }

    SECTION("operator- (iterator - iterator)")
    {
        auto begin = vec.begin();
        auto end = vec.end();
        auto cbegin = vec.cbegin();
        auto cend = vec.cend();

        CHECK(end - begin == static_cast<std::ptrdiff_t>(vec.size()));
        CHECK(cend - cbegin == static_cast<std::ptrdiff_t>(vec.size()));
        CHECK(begin - end == -static_cast<std::ptrdiff_t>(vec.size()));
        CHECK(cbegin - cend == -static_cast<std::ptrdiff_t>(vec.size()));

        CHECK(begin - begin == 0);
        CHECK(cbegin - cbegin == 0);

        CHECK((begin + 5) - begin == 5);
        CHECK((cbegin + 5) - cbegin == 5);
    }

    SECTION("honors the noexcept guarantee")
    {
        auto it = vec.begin();
        auto cit = vec.cbegin();
        CHECK(noexcept(it += 1));
        CHECK(noexcept(cit += 1));
        CHECK(noexcept(it -= 1));
        CHECK(noexcept(cit -= 1));
        CHECK(noexcept(it + 1));
        CHECK(noexcept(cit + 1));
        CHECK(noexcept(1 + it));
        CHECK(noexcept(1 + cit));
        CHECK(noexcept(it - 1));
        CHECK(noexcept(cit - 1));
        CHECK(noexcept(it - it));
        CHECK(noexcept(cit - cit));
    }
}

TEST_CASE("Vector iterator comparison operators", "[vector][iterators]")
{
    forge::vector<int> vec = {10, 20, 30, 40, 50, 60, 70};

    auto b = vec.begin();      // position 0
    auto m1 = vec.begin() + 2; // position 2
    auto m2 = vec.begin() + 2; // same as m1
    auto m3 = vec.begin() + 5; // position 5
    auto e = vec.end();        // past-the-end

    auto cb = vec.cbegin();      // position 0
    auto cm1 = vec.cbegin() + 2; // position 2
    auto cm2 = vec.cbegin() + 2; // same as cm1
    auto cm3 = vec.cbegin() + 5; // position 5
    auto ce = vec.cend();        // past-the-end

    SECTION("operator== and !=")
    {
        CHECK(b == b);
        CHECK(m1 == m2);
        CHECK(m1 != m3);
        CHECK(b != e);
        CHECK(e == e);

        CHECK(cb == cb);
        CHECK(cm1 == cm2);
        CHECK(cm1 != cm3);
        CHECK(cb != ce);
        CHECK(ce == ce);

        CHECK_FALSE(b == m1);
        CHECK_FALSE(m1 == m3);
        CHECK_FALSE(cb == cm1);
        CHECK_FALSE(cm1 == cm3);
    }

    SECTION("operator<, >, <=, >=")
    {
        // Less than
        CHECK(b < m1);
        CHECK(m1 < m3);
        CHECK(m1 < e);
        CHECK_FALSE(m1 < m2); // equal

        CHECK(cb < cm1);
        CHECK(cm1 < cm3);
        CHECK(cm1 < ce);
        CHECK_FALSE(cm1 < cm2);

        // Less than or equal
        CHECK(b <= m1);
        CHECK(m1 <= m2);
        CHECK(m2 <= m1);
        CHECK(m1 <= e);

        CHECK(cb <= cm1);
        CHECK(cm1 <= cm2);
        CHECK(cm2 <= cm1);
        CHECK(cm1 <= ce);

        // Greater than
        CHECK(m3 > m1);
        CHECK(e > m1);
        CHECK_FALSE(b > m1);

        CHECK(cm3 > cm1);
        CHECK(ce > cm1);
        CHECK_FALSE(cb > cm1);

        // Greater than or equal
        CHECK(m3 >= m1);
        CHECK(m2 >= m1);
        CHECK(e >= b);

        CHECK(cm3 >= cm1);
        CHECK(cm2 >= cm1);
        CHECK(ce >= cb);
    }

    SECTION("comparisons with end()")
    {
        CHECK(b < e);
        CHECK(m3 < e);
        CHECK_FALSE(e < e);
        CHECK(e >= e);
        CHECK(e > m2);

        CHECK(cb < ce);
        CHECK(cm3 < ce);
        CHECK_FALSE(ce < ce);
        CHECK(ce >= ce);
        CHECK(ce > cm2);
    }

    SECTION("comparisons between const and non-const iterators")
    {
        CHECK(b == cb);
        CHECK(m1 == cm1);
        CHECK(e == ce);

        CHECK_FALSE(b != cb);
        CHECK_FALSE(m1 != cm1);

        CHECK(b < cm1);
        CHECK(cm1 < m3);

        CHECK(m3 > cm1);
        CHECK(ce > m1);
    }

    SECTION("empty vector comparisons")
    {
        forge::vector<int> empty_vec;

        CHECK(empty_vec.begin() == empty_vec.end());
        CHECK(empty_vec.cbegin() == empty_vec.cend());
        CHECK_FALSE(empty_vec.begin() < empty_vec.end());
        CHECK(empty_vec.begin() <= empty_vec.end());
        CHECK(empty_vec.end() >= empty_vec.begin());
    }

    SECTION("honors the noexcept guarantee")
    {
        CHECK(noexcept(b == m1));
        CHECK(noexcept(m1 != m3));
        CHECK(noexcept(b < m1));
        CHECK(noexcept(m1 <= m2));
        CHECK(noexcept(m3 > m1));
        CHECK(noexcept(e >= b));
    }
}

TEST_CASE("Vector iterator range-based for loop compatibility", "[vector][iterators]")
{
    forge::vector<std::string> vec = {"apple", "banana", "cherry"};

    SECTION("allows iterating over the elements of the vector using a range-based for loop")
    {
        std::vector<std::string> collected_elements;
        for (const auto& element : vec)
        {
            collected_elements.push_back(element);
        }

        CHECK(collected_elements.size() == vec.size());
        CHECK(collected_elements[0] == "apple");
        CHECK(collected_elements[1] == "banana");
        CHECK(collected_elements[2] == "cherry");
    }

    SECTION("works with empty vectors")
    {
        forge::vector<int> empty_vec;
        int sum = 0;
        for (int value : empty_vec)
        {
            sum += value; // This block should not execute since the vector is empty
        }
        CHECK(sum == 0); // Sum should remain zero since there are no elements to iterate over
    }
}

TEST_CASE("Vector begin()/cbegin()", "[vector][iterators]")
{
    SECTION("non-empty vector")
    {
        forge::vector<int> v = {10, 20, 30, 40};

        CHECK(*v.begin() == 10);
        CHECK(*v.cbegin() == 10);

        *v.begin() = 99; // should be mutable
        CHECK(v[0] == 99);
    }

    SECTION("empty vector")
    {
        forge::vector<int> v;

        CHECK(v.begin() == v.end());
        CHECK(v.cbegin() == v.cend());
        CHECK(v.begin() == v.cbegin());
    }

    SECTION("behaves correctly after modifications")
    {
        forge::vector<int> v;

        v.reserve(10);
        CHECK(v.begin() == v.end());

        v.push_back(5);
        CHECK(*v.begin() == 5);

        v.insert(v.begin(), 3, 42);
        CHECK(*v.begin() == 42);

        v.clear();
        CHECK(v.begin() == v.end());
    }

    SECTION("const correctness")
    {
        forge::vector<int> mutable_vec = {1, 2, 3};
        const forge::vector<int>& const_vec = mutable_vec;

        // Non-const vector
        STATIC_REQUIRE(std::is_same_v<decltype(mutable_vec.begin()), forge::vector<int>::iterator>);
        STATIC_REQUIRE(
            std::is_same_v<decltype(mutable_vec.cbegin()), forge::vector<int>::const_iterator>);

        // Const vector
        STATIC_REQUIRE(
            std::is_same_v<decltype(const_vec.begin()), forge::vector<int>::const_iterator>);
        STATIC_REQUIRE(
            std::is_same_v<decltype(const_vec.cbegin()), forge::vector<int>::const_iterator>);
    }

    SECTION("honors the noexcept guarantee")
    {
        forge::vector<int> v;
        CHECK(noexcept(v.begin()));
        CHECK(noexcept(v.cbegin()));
    }
}

TEST_CASE("Vector end()/cend()", "[vector][iterators]")
{
    SECTION("non-empty vector")
    {
        forge::vector<int> v = {10, 20, 30, 40};

        CHECK(*(v.end() - 1) == 40);
        CHECK(*(v.cend() - 1) == 40);
    }

    SECTION("empty vector")
    {
        forge::vector<int> v;

        CHECK(v.end() == v.begin());
        CHECK(v.cend() == v.cbegin());
        CHECK(v.end() == v.cend());
    }

    SECTION("behaves correctly after modifications")
    {
        forge::vector<int> v;

        v.reserve(10);
        CHECK(v.end() == v.begin());

        v.push_back(5);
        CHECK(*(v.end() - 1) == 5);

        v.insert(v.end(), 3, 42);
        CHECK(*(v.end() - 1) == 42);

        v.clear();
        CHECK(v.end() == v.begin());
    }

    SECTION("const correctness")
    {
        forge::vector<int> mutable_vec = {1, 2, 3};
        const forge::vector<int>& const_vec = mutable_vec;

        // Non-const vector
        STATIC_REQUIRE(std::is_same_v<decltype(mutable_vec.end()), forge::vector<int>::iterator>);
        STATIC_REQUIRE(
            std::is_same_v<decltype(mutable_vec.cend()), forge::vector<int>::const_iterator>);

        // Const vector
        STATIC_REQUIRE(
            std::is_same_v<decltype(const_vec.end()), forge::vector<int>::const_iterator>);
        STATIC_REQUIRE(
            std::is_same_v<decltype(const_vec.cend()), forge::vector<int>::const_iterator>);
    }

    SECTION("honors the noexcept guarantee")
    {
        forge::vector<int> v;
        CHECK(noexcept(v.end()));
        CHECK(noexcept(v.cend()));
    }
}

TEST_CASE("Vector reallocate()", "[vector][helpers]")
{
    forge::vector<int> vec;

    SECTION("increases the capacity to 1 from empty")
    {
        forge::detail::vector_tests_accessor<int>::reallocate(
            vec); // Reallocate to a new capacity of 1
        CHECK(vec.capacity() == 1);
    }

    SECTION("increases the capacity to 2 from 1")
    {
        vec.reserve(1); // Ensure the vector starts with a capacity of 1
        forge::detail::vector_tests_accessor<int>::reallocate(
            vec); // Reallocate to a new capacity of 2
        CHECK(vec.capacity() == 2);
    }

    SECTION("increases the capacity by 1.5x")
    {
        vec.reserve(2); // Ensure the vector starts with a capacity of 2

        size_t last_capacity = 2;
        for (int i = 0; i < 25; ++i)
        {
            forge::detail::vector_tests_accessor<int>::reallocate(
                vec); // Reallocate to a new capacity of 1.5x the current capacity
            CHECK(vec.capacity() ==
                  last_capacity + last_capacity / 2); // Capacity should increase by 1.5x
            last_capacity = vec.capacity();
        }
    }
}

TEST_CASE("Vector non-member swap()", "[vector][modifiers]")
{
    forge::vector<int> vec1{1, 2, 3};
    forge::vector<int> vec2{4, 5};

    SECTION("swaps the contents of two vectors, including their size and capacity")
    {
        swap(vec1, vec2); // Use the non-member swap function
        CHECK(vec1.size() == 2);
        CHECK(vec1.capacity() == 2);
        CHECK(vec1[0] == 4);
        CHECK(vec1[1] == 5);

        CHECK(vec2.size() == 3);
        CHECK(vec2.capacity() == 3);
        CHECK(vec2[0] == 1);
        CHECK(vec2[1] == 2);
        CHECK(vec2[2] == 3);
    }

    SECTION("swapping a vector with itself does not change its contents")
    {
        swap(vec1, vec1); // Swap vec1 with itself using the non-member swap
        CHECK(vec1.size() == 3);
        CHECK(vec1.capacity() == 3);
        CHECK(vec1[0] == 1);
        CHECK(vec1[1] == 2);
        CHECK(vec1[2] == 3);
    }

    SECTION("honors the noexcept guarantee")
    {
        CHECK(noexcept(swap(vec1, vec2))); // non-member swap should be noexcept
    }
}

TEST_CASE("Vector randomized fuzzing operations maintain invariants vs std::vector", "[vector]")
{
    using T = int; // Can be changed to other types for more extensive testing

    auto ops_count = GENERATE(take(20, random(100, 1000)));

    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<T> value_dist(-1000, 1000);

    forge::vector<T> vec;
    std::vector<T> std_vec;

    for (int i = 0; i < ops_count; ++i)
    {
        auto op =
            std::uniform_int_distribution{0, 7}(rng); // Randomly select an operation to perform

        switch (op)
        {
        case 0: // push_back
        {
            T value = value_dist(rng);
            vec.push_back(value);
            std_vec.push_back(value);
            break;
        }
        case 1: // emplace_back
        {
            T value = value_dist(rng);
            vec.emplace_back(value);
            std_vec.emplace_back(value);
            break;
        }
        case 2: // pop_back
            if (!vec.empty())
            {
                vec.pop_back();
                std_vec.pop_back();
            }
            break;
        case 3: // insert at random position
            if (!vec.empty())
            {
                size_t pos = std::uniform_int_distribution<size_t>{0, vec.size()}(rng);
                T value = value_dist(rng);
                vec.insert(vec.begin() + pos, value);
                std_vec.insert(std_vec.begin() + pos, value);
            }
            break;
        case 4: // clear
            vec.clear();
            std_vec.clear();
            break;
        case 5: // reserve
        {
            size_t new_capacity = std::uniform_int_distribution<size_t>{0, 100}(rng);
            vec.reserve(new_capacity);
            std_vec.reserve(new_capacity);
            break;
        }
        case 6: // resize
        {
            size_t new_size = std::uniform_int_distribution<size_t>{0, 50}(rng);
            T val = value_dist(rng);
            vec.resize(new_size, val);
            std_vec.resize(new_size, val);
            break;
        }
        case 7: // assign
        {
            size_t new_size = std::uniform_int_distribution<size_t>{0, 50}(rng);
            T val = value_dist(rng);
            vec.assign(new_size, val);
            std_vec.assign(new_size, val);
            break;
        }
        }

        // === INVARIANT CHECKS ===
        REQUIRE(vec.size() == std_vec.size());
        REQUIRE(vec.capacity() >= vec.size());
        REQUIRE(vec.empty() == std_vec.empty());

        if (!vec.empty())
        {
            REQUIRE(vec.front() == std_vec.front());
            REQUIRE(vec.back() == std_vec.back());
        }

        REQUIRE(std::equal(vec.begin(), vec.end(), std_vec.begin(), std_vec.end()));
    }
}

consteval bool test_vector_entire_constexpr_lifecycle()
{
    // 1. Test Default Construction and Emptiness
    forge::vector<int> vec;
    if (!vec.empty() || vec.size() != 0 || vec.capacity() != 0)
    {
        return false;
    }

    // 2. Test Initial Reservations and memory_usage
    vec.reserve(10);
    if (vec.capacity() < 10 || vec.memory_usage() != (vec.capacity() * sizeof(int)))
    {
        return false;
    }

    // 3. Test Direct Back-Insertions (push_back and emplace_back)
    vec.push_back(10);      // const_reference copy path
    vec.push_back(int{20}); // rvalue reference move path
    vec.emplace_back(40);   // In-place construct path

    if (vec.size() != 3 || vec[0] != 10 || vec[1] != 20 || vec[2] != 40)
    {
        return false;
    }

    // 4. Test Element Replacement & Element Access
    vec[1] = 25;
    if (vec.front() != 10 || vec.back() != 40 || vec.data() == nullptr)
    {
        return false;
    }

    // 5. Test Range Insertion
    int extra_elements[] = {5, 6};
    // Insert 5 and 6 at index 1 -> resulting in: {10, 5, 6, 25, 40}
    auto insert_it =
        vec.insert(vec.begin() + 1, std::begin(extra_elements), std::end(extra_elements));

    if (insert_it != (vec.begin() + 1) || vec.size() != 5)
    {
        return false;
    }
    if (vec[0] != 10 || vec[1] != 5 || vec[2] != 6 || vec[3] != 25 || vec[4] != 40)
    {
        return false;
    }

    // 6. Test pop_back
    vec.pop_back(); // Removes 40
    if (vec.size() != 4 || vec.back() != 25)
    {
        return false;
    }

    // 7. Test shrink_to_fit
    vec.shrink_to_fit();
    if (vec.capacity() != vec.size() || vec.size() != 4)
    {
        return false;
    }

    // 8. Test Iterators and Mutability loops
    int sum = 0;
    for (auto it = vec.begin(); it != vec.end(); ++it)
    {
        sum += *it; // 10 + 5 + 6 + 25 = 46
    }
    if (sum != 46)
    {
        return false;
    }

    return true;
}

TEST_CASE("Vector Full Constexpr Lifecycle", "[vector][constexpr]")
{
    SECTION("evaluates all core operations completely at compile-time")
    {
        STATIC_REQUIRE(test_vector_entire_constexpr_lifecycle());
    }
}