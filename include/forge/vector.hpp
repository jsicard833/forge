// SPDX-License-Identifier: MIT

/**
 * @file vector.hpp
 * @author Joshua Sicard
 *
 * @brief An implementation of a dynamic array (vector) that provides comprehensive
 * element access, resizing, and memory management with allocator support.
 *
 * This header provides a production-quality vector template that improves upon
 * std::vector with full support for custom allocators, move semantics, and exception safety and
 * added quality of life features (such as a built in find method). The implementation uses
 * contiguous memory allocation and provides constant-time random access to elements.
 *
 * @details The vector implementation provides:
 * - \f$ O(1) \f$ random access to elements via operator[]
 * - Amortized \f$ O(1) \f$ insertion at the end via push_back()
 * - Strong exception safety for copy operations
 * - Efficient move semantics for rvalue references
 * - Full allocator support with proper propagation
 * - Contiguous iterator support
 */

#pragma once

#include <algorithm>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace forge
{
namespace detail
{
// Forward declaration for test accessor template so it can be friended below
template <typename> struct vector_tests_accessor;

template <typename Alloc, typename T>
concept allocator_has_custom_construct = requires(Alloc& a, T* p, T&& rv) {
    a.construct(p, std::move(rv));
} || requires(Alloc& a, T* p, const T& lv) { a.construct(p, lv); };

template <typename Alloc, typename T>
concept allocator_has_custom_default_construct = requires(Alloc& a, T* p) { a.construct(p); };

template <typename Alloc, typename T>
concept allocator_has_custom_destroy = requires(Alloc& a, T* p) { a.destroy(p); };

template <typename T, typename Alloc>
inline constexpr bool trivially_manipulable_v =
    std::is_trivially_copyable_v<T> && !allocator_has_custom_construct<Alloc, T> &&
    !allocator_has_custom_destroy<Alloc, T>;

template <typename T, typename Alloc>
inline constexpr bool trivially_destructible_v =
    std::is_trivially_destructible_v<T> && !allocator_has_custom_destroy<Alloc, T>;

template <typename T, typename Alloc>
inline constexpr bool trivially_value_initializable_v =
    std::is_trivially_default_constructible_v<T> &&
    !allocator_has_custom_default_construct<Alloc, T> && !allocator_has_custom_destroy<Alloc, T>;
} // namespace detail

/**
 * @brief A dynamic array (vector) container with full support for custom allocators and move
 * semantics.
 *
 * The vector class provides a contiguous, dynamically-resizable array of elements. It maintains
 * three key properties:
 * - **size**: The number of elements currently stored (accessed via size())
 * - **capacity**: The total allocated storage (accessed via capacity())
 * - **data**: Pointer to the dynamically allocated array of elements (accessed via data())
 *
 * Memory is managed through an allocator (defaulting to std::allocator<T>), which is properly
 * propagated on copy/move operations according to allocator traits. The implementation uses a
 * 1.5x growth strategy for capacity expansion to balance memory efficiency and reallocation
 * frequency.
 *
 * @tparam T The type of elements stored in the vector. Must be a valid C++ type that can be
 *           constructed, copied, and destroyed. May be const-qualified.
 * @tparam Alloc The allocator type used for dynamic memory management. Defaults to
 * std::allocator<T>. Must satisfy the Allocator concept from the C++ Standard Library.
 *
 * @note This implementation is exception-safe with the following guarantees:
 *       - Default/move construction: noexcept
 *       - Copy construction: strong exception safety (all-or-nothing)
 *       - push_back/emplace_back: strong exception safety (if an exception is thrown, the vector
 * remains unchanged)
 *       - Operations on empty vectors: noexcept operations remain valid
 *
 * @see std::vector for standard container behavior
 */
template <typename T, typename Alloc = std::allocator<T>> class vector
{
  public:
    // =========================================================
    // Type aliases
    // =========================================================

    using value_type = T;
    using allocator_type = Alloc;
    using difference_type = std::ptrdiff_t;
    using reference = T&;
    using const_reference = const T&;
    using pointer = T*;
    using const_pointer = const T*;
    using size_type = std::size_t;

    template <typename U> class base_iterator;

    using iterator = base_iterator<T>;
    using const_iterator = base_iterator<const T>;

  public:
    // =========================================================
    // Constructors / Destructor
    // =========================================================

    /**
     * @brief Default constructor
     *
     * Constructs an empty vector with no allocated memory. All internal state is initialized to
     * zero, with the data pointer set to nullptr. The default allocator is default-constructed.
     *
     * @complexity \f$ O(1) \f$
     * @exception noexcept(noexcept(Alloc()))
     *
     * @note The resulting vector has size == 0, capacity == 0, and data == nullptr.
     * @note Default construction of the allocator may potentially throw if the allocator type
     *       has a non-trivial constructor.
     */
    constexpr vector() noexcept(noexcept(Alloc()))
        : alloc_(Alloc()), data_(nullptr), size_(0), capacity_(0)
    {
    }

    /**
     * @brief Constructs an empty vector with a specified allocator
     *
     * Creates an empty vector while using the provided allocator for all dynamic memory management.
     * This is useful when using custom allocators with specific memory strategies. Size and
     * capacity are both initialized to zero.
     *
     * @param alloc The allocator instance to use for memory management. Must be a valid allocator
     *              object of type Alloc.
     *
     * @complexity \f$ O(1) \f$
     * @exception noexcept
     *
     * @note Provides a way to explicitly specify the allocator instance for fine-grained control
     *       over memory allocation behavior.
     */
    constexpr explicit vector(const Alloc& alloc) noexcept
        : alloc_(alloc), data_(nullptr), size_(0), capacity_(0)
    {
    }

    /**
     * @brief Constructs a vector with a specified number of default-constructed elements
     *
     * Allocates memory and creates `count` elements using default construction (T()). The resulting
     * vector has both size and capacity equal to `count`. For primitive types (int, double, etc.),
     * default construction leaves values uninitialized; use the two-argument overload if you need
     * zero-initialization.
     *
     * @param count The number of elements to create. Must be within the range [0, max_size()].
     * @param alloc The allocator instance for memory management. Defaults to Alloc().
     *
     * @complexity \f$ O(\mbox{count}) \f$
     * @exception std::length_error if count exceeds max_size()
     * @exception std::bad_alloc if memory allocation fails
     * @exception ... Any exception thrown by T's default constructor
     *
     * @exception_safety Strong exception safety: if any constructor throws, the vector is left
     *                    unchanged, and any partially-constructed elements are properly destroyed.
     *
     * @warning For primitive types, elements are not zero-initialized. Use the two-argument
     *          constructor with a specific value (e.g., vector(count, 0)) for initialized storage.
     * @warning If T's default constructor is expensive, consider vector(count, value) or
     *          reserve() + emplace_back() in a loop.
     */
    constexpr explicit vector(size_type count, const Alloc& alloc = Alloc())
        : alloc_(alloc), size_(0), capacity_(count)
    {
        if (count == 0)
        {
            data_ = nullptr; // No allocation needed for zero elements
            return;
        }
        else if (count > max_size()) [[unlikely]]
            throw std::length_error("Count exceeds maximum size");

        data_ = traits::allocate(alloc_, count);

        if constexpr (detail::trivially_value_initializable_v<T, Alloc>)
        {
            // For trivially default-constructible types, we can skip explicit construction since
            // the memory is already allocated and considered "constructed" in a trivial sense.
            size_ = count;
        }
        else
        {
            // Use a try-catch block to ensure that if an exception is thrown during element
            // construction, we properly clean up any already constructed elements and deallocate
            // the memory to prevent leaks
            try
            {
                for (; size_ < count; ++size_)
                {
                    traits::construct(alloc_, data_ + size_);
                }
            }
            catch (...)
            {
                // Destory any already constructed elements in reverse order of construction
                while (size_ > 0)
                {
                    traits::destroy(alloc_, data_ + (--size_));
                }
                traits::deallocate(alloc_, data_, capacity_); // Deallocate the memory
                throw; // Rethrow the exception to propagate it to the caller
            }
        }
    }

    /**
     * @brief Constructs a vector with a specified count of identical elements
     *
     * Creates a vector containing `count` copies of `value`. Memory is allocated and each element
     * is copy-constructed from the provided value. Both size and capacity are set to `count`.
     *
     * @param count The number of elements to create. Must be within the range [0, max_size()].
     * @param value The value to copy into each element. Must be of type T.
     * @param alloc The allocator instance for memory management. Defaults to Alloc().
     *
     * @complexity \f$ O(\mbox{count}) \f$
     * @exception std::bad_alloc if memory allocation fails
     * @exception std::length_error if count exceeds max_size()
     * @exception ... Any exception thrown by T's copy constructor
     *
     * @exception_safety Strong exception safety: if any constructor throws, the vector is left
     *                    unchanged, and any partially-constructed elements are properly destroyed.
     *
     * @note This constructor is particularly useful for pre-allocating vectors with default
     *       values, such as vector<int>(100, 0) to create a zero-filled array.
     */
    constexpr explicit vector(size_type count, const_reference value, const Alloc& alloc = Alloc())
        : alloc_(alloc), size_(0), capacity_(count)
    {
        if (count == 0)
        {
            data_ = nullptr; // No allocation needed for zero elements
            return;
        }
        else if (count > max_size()) [[unlikely]]
            throw std::length_error("Count exceeds maximum size");

        data_ = traits::allocate(alloc_, count);

        try
        {
            construct_fill(data_, count, value);
            size_ = count;
        }
        catch (...)
        {
            traits::deallocate(alloc_, data_, capacity_);
            throw;
        }
    }

    /**
     * @brief Constructs a vector from a range of elements defined by two iterators
     *
     * Creates a vector containing copies of all elements in the range [first, last).
     * Supports any input iterator type. For random-access iterators, the distance between
     * iterators is computed in \f$ O(1) \f$; for other iterators, it's \f$ O(n) \f$.
     *
     * @tparam InputIt The iterator type, must satisfy std::input_iterator concept.
     *                 Elements must be convertible to T.
     *
     * @param first Iterator to the first element in the range (inclusive).
     * @param last Iterator to one past the last element in the range (exclusive).
     *              Must be reachable from `first` by repeated increments.
     * @param alloc The allocator instance for memory management. Defaults to Alloc().
     *
     * @complexity
     * - \f$ O(n) \f$ element copies/moves for non-trivial types.
     * - \f$ O(1) \f$ algorithmic time (via block memory copies) for trivially copyable types using
     * contiguous iterators.
     * - Reallocations may incur an additional amortized complexity cost if a pure non-forward
     * `std::input_iterator` is utilized.
     *
     * @exception std::bad_alloc if memory allocation fails
     * @exception std::length_error if computed range distance exceeds max_size().
     * @exception ... Any exception thrown by T's copy/move constructor
     *
     * @exception_safety Strong exception safety: if any constructor or allocation throws, the
     * vector remains empty, and no resources or partially constructed elements are leaked.
     *
     * @note The distance between iterators is computed even if the range is empty.
     * @note Undefined behavior if [first, last) is not a valid range or if iterators come from
     *       a different container of a different type.
     *
     * @see assign(InputIt, InputIt) for assigning to an existing vector
     */
    template <typename InputIt>
        requires std::input_iterator<InputIt>
    constexpr vector(InputIt first, InputIt last, const Alloc& alloc = Alloc())
        : alloc_(alloc), data_(nullptr), size_(0), capacity_(0)
    {
        // Safe Path for non-forward iterators (cannot pre-calculate distance)
        if constexpr (std::forward_iterator<InputIt>)
        {
            auto n = static_cast<size_type>(std::distance(first, last));
            if (n == 0)
                return;

            if (n > max_size()) [[unlikely]]
                throw std::length_error("Range size exceeds maximum size");

            data_ = traits::allocate(alloc_, n);
            capacity_ = n;

            try
            {
                copy_construct_range(data_, first, n);
                size_ = n;
            }
            catch (...)
            {
                traits::deallocate(alloc_, data_, capacity_);
                data_ = nullptr;
                capacity_ = 0;
                throw;
            }
        }
        else
        {
            try
            {
                while (first != last)
                {
                    emplace_back(*first);
                    ++first;
                }
            }
            catch (...)
            {
                clear();
                throw;
            }
        }
    }

    /**
     * @brief Constructs a vector from an initializer list
     *
     * Creates a vector initialized with elements from an initializer list. This constructor enables
     * convenient initialization using brace syntax:
     * @code
     * forge::vector<int> v = {1, 2, 3, 4, 5};
     * @endcode
     * The capacity and size both equal the number of elements in the initializer list.
     *
     * @param init The initializer list containing elements to initialize the vector.
     *             Type T must be constructible from the initializer list element type.
     * @param alloc The allocator instance for memory management. Defaults to Alloc().
     *
     * @complexity \f$ O(\mbox{size of init}) \f$
     * @exception std::bad_alloc if memory allocation fails
     * @exception ... Any exception thrown by T's copy constructor
     *
     * @exception_safety Strong exception safety: if construction fails, the vector remains
     *                    empty, and no resources are leaked.
     *
     * @note Provides syntactic convenience for initializing vectors with known elements at
     *       construction time.
     */
    constexpr vector(std::initializer_list<T> init, const Alloc& alloc = Alloc())
        : vector(init.begin(), init.end(), alloc)
    {
    }

    /**
     * @brief Copy constructor - creates a deep copy of another vector
     *
     * Constructs a new vector as a complete independent copy of `other`. All elements are copied
     * from the source vector, and new memory is allocated. The allocator is selected according to
     * allocator traits via `select_on_container_copy_construction` (SOCCC), which delegates the
     * choice of whether to propagate the source allocator to the allocator type itself.
     * The size of the new vector will match the source while the capacity will shrink to fit.
     *
     * @param other The source vector to copy from. Must be a valid vector object (may be empty).
     *
     * @complexity \f$ O(\mbox{size of other}) \f$
     * @exception std::bad_alloc if memory allocation fails
     * @exception ... Any exception thrown by T's copy constructor
     *
     * @exception_safety Strong exception safety: if construction fails, the new vector remains
     *                    empty and no resources are leaked.
     *
     * @note The new vector is completely independent of the original. Modifications to either
     *       vector do not affect the other.
     * @note Uses allocator_traits::select_on_container_copy_construction for allocator propagation,
     *       which may or may not propagate the allocator depending on the allocator type.
     */
    constexpr vector(const vector& other)
        : vector(other.data_, other.data_ + other.size_,
                 traits::select_on_container_copy_construction(other.alloc_))
    {
    }

    /**
     * @brief Move constructor - efficiently transfers ownership from another vector
     *
     * Constructs a new vector by efficiently transferring ownership of the internal resources
     * from `other`. The source vector is left in a valid but empty state. No element copying
     * occurs, making this operation very efficient.
     *
     * @param other The source vector to move from will be left in a valid but empty state.
     *
     * @complexity \f$ O(1) \f$
     * @exception noexcept(std::is_nothrow_move_constructible_v<Alloc>)
     *
     * @note The moved-from vector should not be used until its state is explicitly reset.
     * @note Allocator is moved (not copied), so the new vector takes ownership of the allocator.
     */
    constexpr vector(vector&& other) noexcept(std::is_nothrow_move_constructible_v<Alloc>)
        : alloc_(std::move(other.alloc_)), data_(std::exchange(other.data_, nullptr)),
          size_(std::exchange(other.size_, 0)), capacity_(std::exchange(other.capacity_, 0))
    {
    }

    /**
     * @brief Destructor - frees all resources
     *
     * Destroys all contained elements by calling their destructors, then deallocates the
     * underlying memory through the allocator. If the vector was never allocated (size and
     * capacity both zero), this is a no-op.
     *
     * @complexity \f$ O(\mbox{size}) \f$ for non-trivial types, \f$ O(1) \f$ for trivial types.
     */
    constexpr ~vector() noexcept
    {
        if (data_)
        {
            if constexpr (!detail::trivially_destructible_v<value_type, Alloc>)
            {
                for (size_type i = 0; i < size_; ++i)
                {
                    traits::destroy(alloc_, data_ + i);
                }
            }

            traits::deallocate(alloc_, data_, capacity_);
        }
    }

  public:
    // =========================================================
    // Assignment operators
    // =========================================================

    /**
     * @brief Copy assignment operator - replaces contents with a deep copy
     *
     * Replaces the contents of this vector with a deep copy of `other`. Uses the copy-and-swap
     * idiom to ensure strong exception safety. All existing elements are destroyed and the old
     * memory is deallocated before new memory is allocated and populated.
     *
     * @param other The source vector to copy from (may be the same as *this).
     * @return A reference to *this, allowing for chaining assignments.
     *
     * @complexity \f$ O(\mbox{size of other}) \f$
     * @exception std::bad_alloc if memory allocation fails
     * @exception ... Any exception thrown by T's copy constructor
     *
     * @exception_safety Strong exception safety: if assignment fails, *this remains unchanged.
     *                    This is achieved using copy-and-swap semantics.
     *
     * @note The allocator may or may not be propagated, depending on allocator traits.
     *
     * @see swap(vector&) for the underlying swap operation
     */
    constexpr auto operator=(const vector& other) -> vector&
    {
        if (this != &other) [[likely]]
        {
            // Use the copy-and-swap idiom to provide strong exception safety. We create a temporary
            // copy of the source vector, and then swap its contents with the current vector. If any
            // exceptions are thrown during the copying process, the current vector remains
            // unchanged, and we avoid memory leaks.
            vector temp(other);

            this->swap(
                temp); // Swap the contents of the temporary vector with the current vector. This
                       // provides strong exception safety, as any exceptions thrown during the
                       // construction of the temporary vector will not affect the current vector.
        }

        return *this;
    }

    /**
     * @brief Move assignment operator - transfers ownership per allocator-aware container rules
     *
     * Replaces the contents of this vector with those of `other`. The exact strategy
     * depends on the allocator's propagation traits:
     *
     *  - If `propagate_on_container_move_assignment` is `true`, this vector releases its
     *    own storage, adopts `other`'s allocator, and steals `other`'s buffer outright.
     *  - Else if the allocators compare equal (or `is_always_equal` is `true`), this
     *    vector keeps its own allocator but can still steal `other`'s buffer, since the
     *    allocator that freed it doesn't matter.
     *  - Otherwise (unequal, non-propagating allocators), this vector cannot take
     *    ownership of `other`'s memory at all. It instead move-assigns element-by-element
     *    into its own (possibly reused, possibly reallocated) storage using its own
     *    allocator, leaving `other`'s storage and size untouched but its elements
     *    individually moved-from.
     *
     * @param other The source vector to move from. Will be left in a valid but empty state.
     * @return A reference to *this, allowing for chaining assignments.
     *
     * @complexity O(1) on the propagating or equal-allocator paths (pointer/allocator swap only).
     *             O(n) on the unequal, non-propagating allocator fallback path, with possible
     *             reallocation if `other.size() > capacity()`.
     *
     * @exception
     * noexcept(std::allocator_traits<Alloc>::propagate_on_container_move_assignment::value
     *                      || std::allocator_traits<Alloc>::is_always_equal::value)
     *            The fallback path is only reachable when this expression is `false`, since an
     *            unequal, non-propagating allocator implies elements may need to be individually
     *            move-assigned/move-constructed and storage may need to be (re)allocated, either of
     *            which can throw depending on `T`'s move constructor/assignment and the allocator.
     *
     * @note On the propagating and equal-allocator paths, `other` is left valid and empty
     *       (`size() == 0`), matching the typical "moved-from vector" expectation.
     * @note On the unequal, non-propagating allocator fallback path, `other` is left in a
     *       valid but *unspecified* state: its size is unchanged, but each element has been
     *       individually moved-from and should not be relied upon for its prior value.
     * @note Allocators are propagated (not swapped) when `propagate_on_container_move_assignment`
     *       is `true`; otherwise this vector's own allocator is preserved for the lifetime of
     *       the call, in line with allocator-aware container requirements.
     *
     * @see swap(vector&) for the underlying operation
     */
    constexpr auto
    operator=(vector&& other) noexcept(traits::propagate_on_container_move_assignment::value ||
                                       traits::is_always_equal::value) -> vector&
    {
        if (this != &other) [[likely]]
        {
            if constexpr (traits::propagate_on_container_move_assignment::value)
            {
                // POCMA true: we're required to take over the allocator,
                // so stealing the buffer is always safe.
                clear();
                traits::deallocate(alloc_, data_, capacity_);
                alloc_ = std::move(other.alloc_);
                data_ = std::exchange(other.data_, nullptr);
                size_ = std::exchange(other.size_, 0);
                capacity_ = std::exchange(other.capacity_, 0);
            }
            else if (traits::is_always_equal::value || alloc_ == other.alloc_)
            {
                // Allocators are interchangeable, so keep our own allocator and steal the buffer.
                clear();
                traits::deallocate(alloc_, data_, capacity_);
                data_ = std::exchange(other.data_, nullptr);
                size_ = std::exchange(other.size_, 0);
                capacity_ = std::exchange(other.capacity_, 0);
            }
            else
            {
                // Allocators differ and won't propagate, so we can't steal the buffer. Instead, we
                // need to move-assign each element individually. This is less efficient but
                // necessary to maintain correct allocator semantics.
                assign(std::make_move_iterator(other.begin()),
                       std::make_move_iterator(other.end()));
            }
        }
        return *this;
    }

  public:
    // =========================================================
    // Element access
    // =========================================================

    /**
     * @brief Provides unchecked access to an element by index using array subscript syntax
     *
     * Allows efficient direct access to elements using operator[] syntax. This function leverages
     * C++23 explicit object parameters ("deducing this") to unify const, non-const, lvalue, and
     * rvalue access into a single implementation without duplicating code.
     *
     * @param self The deduced instance of the vector (handles cv-ref qualifiers automatically).
     * @param index The zero-based index of the element to access.
     *              Must be less than size(); behavior is undefined otherwise.
     * @return A forwarded reference to the element at the specified index (`T&`, `const T&`, `T&&`,
     * or `const T&&` depending on how the vector itself was accessed).
     *
     * @complexity \f$ O(1) \f$
     * @exception noexcept
     *
     * @note **Debug Hardening:** If compiled in debug mode (where `NDEBUG` is not defined),
     * this operator will perform a swift boundary check via an internal `assert`.
     * In release configurations, this assertion is stripped out entirely, meaning out-of-bounds
     * access will result in classic Undefined Behavior (UB) to maintain zero-overhead performance.
     * @note For safety-critical code where runtime bounds-checking is strictly mandatory in
     * production, use the `at(size_type)` member function instead.
     *
     * @see at(size_type) for production-enforced bounds-checked access
     */
    [[nodiscard]] constexpr auto operator[](this auto&& self,
                                            size_type index) noexcept -> decltype(auto)
    {
        // We use an assert here to catch out-of-bounds access in debug builds. In release builds,
        // this will be a no-op, and the behavior will be undefined if the index is out of bounds.
        // This allows us to provide maximum performance while still catching errors during
        // development.
        assert(index < self.size_ && "forge::vector::operator[] index out of bounds!");
        return self.data_[index];
    }

    /**
     * @brief Provides bounds-checked access to an element by index
     *
     * Safely accesses an element by index, throwing std::out_of_range if the index is invalid.
     * Uses C++23 explicit object parameters to unify const and non-const overloads into a
     * single high-performance implementation.
     *
     * @param self The deduced instance of the vector (handles cv-ref qualifiers automatically).
     * @param index The zero-based index of the element to access.
     *              Must be less than size().
     * @return A forwarded reference to the element at the specified index (`T&`, `const T&`, `T&&`,
     * or `const T&&` depending on how the vector itself was accessed).
     *
     * @complexity \f$ O(1) \f$
     * @exception std::out_of_range if index >= size()
     *
     * @note Use this method when the index source is not under your control or when safety is
     *       more important than maximum performance.
     *
     * @see operator[](size_type) for unchecked access
     */
    [[nodiscard]] constexpr auto at(this auto&& self, size_type index) -> decltype(auto)
    {
        if (index >= self.size_) [[unlikely]]
            throw std::out_of_range("forge::vector::at: index " + std::to_string(index) +
                                    " is out of bounds for size " + std::to_string(self.size()));
        return self.data_[index];
    }

    /**
     * @brief Provides unchecked access to the first element
     *
     * Returns a reference to the first element in the vector, providing maximum efficiency for
     * direct element access. This function leverages C++23 explicit object parameters ("deducing
     * this") to automatically unify const, non-const, lvalue, and rvalue variants into a single,
     * clean implementation.
     *
     * @param self The deduced instance of the vector (handles cv-ref qualifiers automatically).
     *
     * @return A forwarded reference to the first element of the vector (`T&`, `const T&`,
     * `T&&`, or `const T&&` depending on the cv-ref qualification of the calling object).
     *
     * @complexity \f$ O(1) \f$
     * @exception noexcept
     *
     * @pre The vector must not be empty (`size() > 0`).
     *
     * @note **Debug Hardening:** If compiled in a debug configuration (where `NDEBUG` is not
     * defined), this function executes a defensive boundary validation via an internal `assert`. In
     * release builds, this assertion is stripped out entirely, meaning calling `front()` on an
     * empty vector results in classic Undefined Behavior (UB) to maintain optimal performance.
     *
     * @warning Calling this function on an empty vector in release mode guarantees undefined
     * behavior. Always check `empty()` beforehand if the container's operational state is
     * uncertain.
     *
     * @see back() for accessing the last element
     */
    [[nodiscard]] constexpr auto front(this auto&& self) noexcept -> decltype(auto)
    {
        assert(self.size_ > 0 && "forge::vector::front() called on empty vector!");
        return self.data_[0];
    }

    /**
     * @brief Provides unchecked access to the last element
     *
     * Returns a reference to the last element in the vector (at index `size() - 1`), providing
     * maximum efficiency for direct rear-element access. This function leverages C++23 explicit
     * object parameters ("deducing this") to automatically unify const, non-const, lvalue,
     * and rvalue variants into a single, cohesive implementation.
     *
     * @param self The deduced instance of the vector (handles cv-ref qualifiers automatically).
     *
     * @return A forwarded reference to the last element of the vector (`T&`, `const T&`,
     * `T&&`, or `const T&&` depending on the cv-ref qualification of the calling object).
     *
     * @complexity \f$ O(1) \f$
     * @exception noexcept
     *
     * @pre The vector must not be empty (`size() > 0`).
     *
     * @note **Debug Hardening:** If compiled in a debug configuration (where `NDEBUG` is not
     * defined), this function executes a defensive boundary validation via an internal `assert`. In
     * release builds, this assertion is stripped out entirely, meaning calling `back()` on an empty
     * vector results in classic Undefined Behavior (UB) to maintain optimal performance.
     *
     * @warning Calling this function on an empty vector in release mode guarantees undefined
     * behavior. Always check `empty()` beforehand if the container's operational state is
     * uncertain.
     *
     * @see front() for accessing the first element of the vector
     */
    [[nodiscard]] constexpr auto back(this auto&& self) noexcept -> decltype(auto)
    {
        assert(self.size_ > 0 && "forge::vector::back() called on empty vector!");
        return self.data_[self.size_ - 1];
    }

    /**
     * @brief Provides direct pointer access to the underlying data array
     *
     * Returns a pointer to the first element of the underlying array, allowing direct pointer
     * manipulation and out-of-the-box compatibility with C-style APIs. This function leverages
     * C++23 explicit object parameters ("deducing this") to automatically unify const and
     * non-const overloads into a single implementation.
     *
     * @param self The deduced instance of the vector (handles cv-ref qualifiers automatically).
     * @return A forwarded pointer to the underlying array (`T*` or `const T*` depending on the
     * cv-qualification of the calling vector instance). Returns `nullptr` if the vector
     * has no allocated capacity.
     *
     * @complexity \f$ O(1) \f$
     * @exception noexcept
     *
     * @note **Pointer Validity:** The returned pointer is strictly valid only as long as no
     * reallocation occurs. Reallocation can be triggered by operations such as `push_back()`,
     * `insert()`, `emplace_back()`, `resize()`, `reserve()`, or similar capacity-modifying
     * functions.
     * @note For an empty vector with non-zero capacity, `data()` returns a valid pointer to the
     * allocated buffer but accessing elements remains undefined behavior as `size() == 0`.
     *
     * @see get_view() for a safer, modern alternative via std::span
     */
    [[nodiscard]] constexpr auto data(this auto&& self) noexcept -> decltype(self.data_)
    {
        return self.data_;
    }

    /**
     * @brief Returns a std::span view of all active elements in the vector
     *
     * Creates a non-owning `std::span` that acts as a safe, viewable window over the vector's
     * elements. This function leverages C++23 explicit object parameters ("deducing this") to
     * automatically unify const and non-const overloads, preserving array boundaries alongside data
     * access.
     *
     * @param self The deduced instance of the vector (handles cv-ref qualifiers automatically).
     * @return A `std::span<T>` or `std::span<const T>` (depending on the cv-qualification of the
     * calling vector instance) viewing the contiguous range `[data(), data() + size())`.
     *
     * @complexity \f$ O(1) \f$
     * @exception noexcept
     *
     * @note This serves as a modern, type-safe replacement for raw pointer manipulation by
     * encapsulating bounds information directly. It is ideal for passing data to functions
     * expecting slice or span semantics.
     * @note The returned span is immediately invalidated if any operation causes an internal vector
     * reallocation. For completely empty vectors, the returned span is valid but retains a size of
     * zero.
     *
     * @see data() for raw pointer access
     * @see std::span for C++ standard library span specifications
     */
    [[nodiscard]] constexpr auto get_view(this auto&& self) noexcept
    {
        using element_t =
            std::conditional_t<std::is_const_v<std::remove_reference_t<decltype(self)>>, const T,
                               T>;
        return std::span<element_t>(self.data_, self.size_);
    }

    /**
     * @brief Returns the allocator instance used by the vector
     *
     * Provides access to the allocator for advanced users who need to interact with it directly.
     *
     * @return The allocator instance used for memory management.
     *
     * @complexity \f$ O(1) \f$
     * @exception noexcept
     *
     * @note This allows users to query the allocator for properties, perform custom allocations, or
     *       use allocator-aware utilities. For most users, this is not commonly needed.
     */
    [[nodiscard]] constexpr auto get_allocator() const noexcept -> allocator_type
    {
        return alloc_;
    }

  public:
    // =========================================================
    // Capacity
    // =========================================================

    /**
     * @brief Returns the number of elements currently stored in the vector
     *
     * Returns the exact count of active elements managed by the container, not the
     * underlying allocated capacity.
     *
     * @return The current size (number of populated elements).
     *
     * @complexity \f$ O(1) \f$
     * @exception noexcept
     *
     * @note For validating if a container has zero elements, checking `empty()` is
     * semantically preferred and more idiomatic.
     *
     * @see capacity() to query total allocated memory blocks
     * @see empty() for an explicit boolean emptiness check
     */
    [[nodiscard]] constexpr auto size() const noexcept -> size_type
    {
        return size_;
    }

    /**
     * @brief Returns the theoretical maximum number of elements the vector can hold
     *
     * Calculates the system and allocator-imposed upper boundary for the container's size.
     * This accounts for allocator limits, the maximum value representable by `size_type`,
     * and pointer arithmetic distance limitations (`difference_type`).
     *
     * @return The maximum possible size the vector can achieve.
     *
     * @complexity \f$ O(1) \f$
     * @exception noexcept
     *
     * @note Hardened to ensure that `max_size() * sizeof(value_type)` can never overflow
     * pointer arithmetic boundaries, keeping all iterator offsets entirely safe from undefined
     * behavior.
     *
     * @see capacity() for the currently allocated size
     * @see size() for the number of elements currently stored
     */
    [[nodiscard]] constexpr auto max_size() const noexcept -> size_type
    {
        // 1. Absolute maximum elements allowed by the pointer distance model (signed limit)
        const auto pointer_dist_limit =
            static_cast<size_type>(std::numeric_limits<difference_type>::max());

        // 2. Absolute maximum elements allowed by physical addressing limits (unsigned limit /
        // element size)
        const size_type allocation_limit =
            std::numeric_limits<size_type>::max() / sizeof(value_type);

        // 3. Combine them to find the true structural hardware limit
        const size_type hardware_limit = std::min(pointer_dist_limit, allocation_limit);

        // 4. Return the stricter of the allocator's internal limit and the system hardware limit
        return std::min(traits::max_size(alloc_), hardware_limit);
    }

    /**
     * @brief Returns the total allocated storage capacity for elements
     *
     * Returns the number of elements that can be stored without reallocation. Always >= size().
     * The difference (capacity() - size()) is the "free" space that can hold new elements before
     * reallocation becomes necessary.
     *
     * @return The current allocated capacity.
     *
     * @complexity \f$ O(1) \f$
     * @exception noexcept
     *
     * @note To minimize reallocation overhead, capacity may be significantly larger than size().
     * @note Use reserve() to pre-allocate capacity if you know the future size.
     *
     * @see size() for the number of elements currently stored
     * @see reserve(size_type) to request additional capacity
     */
    [[nodiscard]] constexpr auto capacity() const noexcept -> size_type
    {
        return capacity_;
    }

    /**
     * @brief Checks whether the vector contains no elements
     *
     * Returns true if size() == 0, false otherwise. This is the idiomatic way to check if a
     * vector is empty (preferred over "size() == 0").
     *
     * @return true if the vector is empty; false otherwise.
     *
     * @complexity \f$ O(1) \f$
     * @exception noexcept
     */
    [[nodiscard]] constexpr auto empty() const noexcept -> bool
    {
        return size_ == 0;
    }

    /**
     * @brief Returns the total bytes of memory allocated for the element storage
     *
     * Calculates and returns the memory footprint of the container in bytes. By default,
     * it measures the raw heap allocation buffer (`capacity() * sizeof(T)`). It can optionally
     * include the stack overhead of the vector's control block itself for precise tracking.
     *
     * @param include_control_block If true, adds `sizeof(*this)` to include the stack overhead
     * of the vector object structures (pointers, size tracker, etc.).
     * @return Total bytes allocated/consumed by the vector.
     *
     * @complexity \f$ O(1) \f$
     * @exception noexcept
     *
     * @note This reflects calculated structural memory allocations. It cannot account for
     * external allocator padding, heap-manager tracking cookies, or internal fragmentation.
     *
     * @see capacity() for the number of elements currently accommodated in the buffer
     */
    [[nodiscard]] constexpr auto
    memory_usage(bool include_control_block = false) const noexcept -> size_type
    {
        const size_type heap_bytes = capacity_ * sizeof(value_type);

        if (include_control_block)
        {
            return heap_bytes + sizeof(*this);
        }

        return heap_bytes;
    }

    /**
     * @brief Requests sufficient capacity to hold at least `new_capacity` elements
     *
     * If new_capacity > capacity(), allocates new memory, moves existing elements to it,
     * and deallocates the old memory. If new_capacity <= capacity(), this is a no-op.
     * This operation does not change size(), only capacity().
     *
     * @param new_capacity The desired minimum capacity. Must not exceed max_size().
     *
     * @complexity \f$ O(\mbox{size}) \f$ if reallocation occurs; \f$ O(1) \f$ if not
     * @exception std::length_error if new_capacity > max_size()
     * @exception std::bad_alloc if memory allocation fails
     * @exception .. Any exception thrown by T's move/copy constructor
     *
     * @exception_safety Strong exception safety: if reallocation fails, the vector is unchanged.
     *
     * @note Call this before a series of push_back() operations if you know the final size,
     *       to avoid multiple reallocations.
     * @note Using std::move_if_noexcept, elements are moved when possible; copied otherwise.
     * @note Invalidates all iterators and pointers if reallocation occurs.
     *
     * @see shrink_to_fit() to reduce capacity to match size()
     */
    constexpr auto reserve(size_type new_capacity) -> void
    {
        if (new_capacity > max_size()) [[unlikely]]
            throw std::length_error(
                "forge::vector::reserve: requested capacity exceeds max_size()");

        if (new_capacity > capacity_)
        {
            pointer new_data = traits::allocate(alloc_, new_capacity);

            try
            {
                relocate_elements(new_data);
            }
            catch (...)
            {
                traits::deallocate(alloc_, new_data, new_capacity);
                throw;
            }

            if (data_)
            {
                if constexpr (!detail::trivially_manipulable_v<value_type, Alloc>)
                {
                    for (size_type j = 0; j < size_; ++j)
                        traits::destroy(alloc_, data_ + j);
                }
                traits::deallocate(alloc_, data_, capacity_);
            }

            capacity_ = new_capacity;
            data_ = new_data;
        }
    }

    /**
     * @brief Reduces capacity to match size, releasing excess allocated memory
     *
     * If capacity() > size(), allocates new memory exactly sized for current elements, moves them,
     * and deallocates the old memory. If capacity() == size(), this is a no-op.
     *
     * @complexity \f$ O(n) \f$ if shrinking occurs; \f$ O(1) \f$ if not
     * @exception std::bad_alloc if the new allocation fails
     * @exception ... Any exception thrown by T's move constructor
     *
     * @exception_safety Strong exception safety: on exception, the vector is unchanged.
     *
     * @note This is useful to free "wasted" capacity after removing many elements, or as part
     *       of a memory optimization pass.
     * @note Invalidates all iterators and pointers if reallocation occurs.
     *
     * @see reserve(size_type) for the opposite operation
     */
    constexpr auto shrink_to_fit() -> void
    {
        if (capacity_ == size_)
            return; // No excess capacity to free, so we can skip the entire process.

        // Create a temporary vector that is exactly the size of our current elements.
        // The copy constructor (or move-based constructor) will only allocate
        // as much as it needs.
        vector temp(std::make_move_iterator(begin()), std::make_move_iterator(end()), alloc_);

        this->swap(temp);
    }

  public:
    // =========================================================
    // Modifiers
    // =========================================================

    /**
     * @brief Adds a copy of `value` to the end of the vector
     *
     * Appends a copy of the provided value to the end of the vector. If the vector is at capacity,
     * reallocation occurs automatically using the 1.5x growth strategy. The size is incremented
     * after successful insertion.
     *
     * @param value The value to append. Must be copy-constructible.
     *
     * @complexity Amortized \f$ O(1) \f$ - \f$ O(n) \f$ only when reallocation occurs
     * @exception std::bad_alloc if reallocation needed but allocation fails
     * @exception ... Any exception thrown by T's copy constructor
     *
     * @exception_safety Strong exception safety: if construction fails, the vector is unchanged.
     *
     * @note Invalidates end() iterator if no reallocation occurs and all iterators and pointers if
     * reallocation does occur.
     *
     * @see push_back(T&&) for move semantics
     * @see emplace_back(Args...) for in-place construction
     * @see pop_back() to remove the last element
     */
    constexpr auto push_back(const_reference value) -> void
        requires std::copy_constructible<T>
    {
        emplace_back(value);
    }

    /**
     * @brief Adds an rvalue reference to the end of the vector using move semantics
     *
     * Appends an rvalue reference to the vector by moving it into the new element.
     * More efficient than the copy overload when you have a temporary or std::move(value).
     * Automatic reallocation with 1.5x growth if at capacity.
     *
     * @param value An rvalue reference to the value to append. Will be moved from.
     *
     * @complexity Amortized \f$ O(1) \f$ - \f$ O(n) \f$ only when reallocation occurs
     * @exception std::bad_alloc if reallocation needed but allocation fails
     * @exception ... Any exception thrown by T's move constructor
     *
     * @exception_safety Strong exception safety: if construction fails, the vector is unchanged.
     *
     * @note Invalidates end() iterator if no reallocation occurs and all iterators and pointers if
     * reallocation does occur.
     *
     * @see push_back(const_reference) for copying
     * @see emplace_back(Args...) for in-place construction
     * @see pop_back() to remove the last element
     */
    constexpr auto push_back(T&& value) -> void
        requires std::move_constructible<T>
    {
        emplace_back(std::move(value));
    }

    /**
     * @brief Constructs an element in-place at the end of the vector
     *
     * Constructs a new element directly in allocated memory at the end of the vector, avoiding
     * a temporary copy/move. The constructor for T is called with the forwarded arguments.
     * If at capacity, automatic reallocation occurs. Returns a reference to the newly constructed
     * element.
     *
     * @tparam Args Types of arguments to forward to T's constructor. The new element will be
     *              constructed as T(std::forward<Args>(args)...).
     * @param args Arguments to forward to the constructor. Must be valid constructor arguments.
     * @return A reference to the newly constructed element at the end of the vector.
     *
     * @complexity Amortized \f$ O(1) \f$ - \f$ O(n) \f$ only when reallocation occurs
     * @exception std::bad_alloc if reallocation needed but allocation fails
     * @exception ... Any exception thrown by T's constructor
     *
     * @exception_safety Strong exception safety: if construction fails, the vector is unchanged.
     *
     * @note More efficient than push_back() when construction requires temporary creation.
     *       Example: vec.emplace_back(x, y, z) instead of vec.push_back(T(x, y, z))
     * @note Invalidates end() iterator if no reallocation occurs and all iterators and pointers if
     * reallocation does occur.
     *
     * @see push_back(const_reference) and push_back(T&&) for simpler element addition
     * @see pop_back() to remove the last element
     */
    template <typename... Args>
    constexpr auto emplace_back(Args&&... args) -> reference
        requires std::constructible_from<T, Args...>
    {
        if (size_ < capacity_)
        {
            traits::construct(alloc_, data_ + size_, std::forward<Args>(args)...);
            return data_[size_++];
        }
        // Reallocation path: delegate to a helper that constructs the new element
        // into the new buffer *before* the old buffer is released, making
        // self-insertion (e.g. vec.emplace_back(vec[0])) well-defined.
        return emplace_back_realloc(std::forward<Args>(args)...);
    }

    /**
     * @brief Inserts a copy of `value` at the specified position
     *
     * Inserts a copy of the provided value at the position indicated by `pos`. Elements at and
     * after `pos` are shifted to the right to make space for the new element. If the vector is at
     * capacity, reallocation occurs automatically.
     *
     * @param pos A const_iterator indicating the position to insert before. Must be a valid
     * iterator into this vector.
     * @param value The value to insert. Must be copy-constructible.
     * @return An iterator pointing to the newly inserted element.
     *
     * @complexity \f$ O(\mbox{size}) \f$
     *
     * @exception std::bad_alloc if reallocation needed but allocation fails
     * @exception ... Any exception thrown by T's copy constructor or move constructor during
     * shifting
     *
     * @exception_safety Strong exception safety: if insertion fails, the vector is unchanged.
     *
     * @note Invalidates iterators at and after `pos` and all pointers if reallocation occurs.
     *
     * @see emplace(const_iterator, Args...) for in-place construction at arbitrary positions
     */
    constexpr auto insert(const_iterator pos, const_reference value) -> iterator
        requires std::copy_constructible<T>
    {
        return emplace(pos, value);
    }

    /**
     * @brief Inserts an rvalue reference at the specified position using move semantics
     *
     * Inserts an rvalue reference at the position indicated by `pos` by moving it into place.
     * Elements at and after `pos` are shifted to the right to make space for the new element. If
     * the vector is at capacity, reallocation occurs automatically.
     *
     * @param pos A const_iterator indicating the position to insert before. Must be a valid
     * iterator into this vector.
     * @param value An rvalue reference to the value to insert. Will be moved from
     * @return An iterator pointing to the newly inserted element.
     *
     * @complexity \f$ O(\mbox{size}) \f$
     *
     * @exception std::bad_alloc if reallocation needed but allocation fails
     * @exception ... Any exception thrown by T's move constructor or copy constructor during
     * shifting
     *
     * @exception_safety Strong exception safety: if insertion fails, the vector is unchanged.
     *
     * @note Invalidates iterators at and after `pos` and all pointers if reallocation occurs.
     *
     * @see emplace(const_iterator, Args...) for in-place construction at arbitrary positions
     */
    constexpr auto insert(const_iterator pos, T&& value) -> iterator
        requires std::move_constructible<T>
    {
        return emplace(pos, std::move(value));
    }

    /**
     * @brief Inserts `count` copies of `value` at the specified position
     *
     * Inserts `count` copies of the provided value at the position indicated by `pos`. Elements at
     * and after `pos` are shifted to the right to make space for the new elements. If the vector is
     * at capacity, reallocation occurs automatically.
     *
     * @param pos A const_iterator indicating the position to insert before. Must be a valid
     * iterator into this vector.
     * @param count The number of copies to insert. Must be non-negative.
     * @param value The value to insert. Must be copy-constructible.
     * @return An iterator pointing to the first of the newly inserted elements or `pos` if `count`
     * is zero.
     *
     * @complexity \f$ O(\mbox{size} + \mbox{count}) \f$
     *
     * @exception std::length_error if size() + count exceeds max_size()
     * @exception std::bad_alloc if reallocation needed but allocation fails
     * @exception ... Any exception thrown by T's copy constructor or move constructor during
     * shifting or
     *
     * @exception_safety Strong exception safety: if insertion fails, the vector is unchanged.
     *
     * @note Invalidates iterators at and after `pos` and all pointers if reallocation occurs.
     */
    constexpr auto insert(const_iterator pos, size_type count, const_reference value) -> iterator
        requires std::copy_constructible<T>
    {
        const T value_copy(value);
        return insert_n(
            pos, count, [&](pointer dst) { construct_fill(dst, count, value_copy); },
            [&](vector& temp)
            {
                for (size_type i = 0; i < count; ++i)
                    temp.emplace_back(value_copy);
            });
    }

    /**
     * @brief Inserts a range of elements at the specified position
     *
     * Inserts elements from the range [first, last) at the position indicated by `pos`.
     *
     * Two strategies are used depending on InputIt's capabilities:
     *  - If InputIt is at least a forward_iterator, the distance is computed up front
     *    and the work is delegated to the same insert_n() helper used by
     *    insert(pos, count, value) — shifting the tail and constructing the new
     *    elements directly into the vacated gap, with a memmove-equivalent fast path
     *    for trivially copyable T outside constant evaluation.
     *  - If InputIt is only a single-pass input_iterator, neither the distance nor a
     *    second traversal is available, so the new elements are instead appended one
     *    at a time to the end of the vector via emplace_back (consuming the iterator
     *    exactly once), and the resulting tail block is then moved into position `pos`
     *    via std::rotate.
     *
     * @param pos A const_iterator indicating the position to insert before. Must be a valid
     * iterator into this vector.
     * @param first An input iterator to the beginning of the range to insert. Must be
     * dereferenceable and must yield values convertible to T.
     * @param last An input iterator to the end of the range to insert. Must be reachable from
     * `first`.
     * @return An iterator pointing to the first of the newly inserted elements or `pos` if the
     * range is empty.
     *
     * @complexity
     * - Forward-or-better InputIt: O(size + distance(first, last)), matching insert_n.
     * - Single-pass InputIt: O(size + distance(first, last)) amortized for the
     *   emplace_back phase, plus an additional O(size + distance(first, last)) for
     *   the final std::rotate — a larger constant factor than the forward-iterator
     *   path since rotate is a more general (swap-based) operation than a directional
     *   shift.
     *
     * @exception std::length_error if size() + distance(first, last) exceeds max_size()
     * @exception std::bad_alloc if reallocation needed but allocation fails
     * @exception ... Any exception thrown by T's copy/move constructor during shifting or
     * construction
     *
     * @exception_safety
     * - Forward-or-better InputIt: Strong — if insertion fails, the vector is unchanged.
     * - Single-pass InputIt: Strong for failures during the append phase (any
     *   newly-appended elements are popped back off before the exception propagates,
     *   restoring the original size and contents exactly). Only the basic guarantee
     *   (valid, but unspecified, contents and order) if std::rotate itself throws —
     *   possible only when T's move construction/assignment can throw. This matches
     *   the documented behavior of std::vector's own InputIt-range insert overload.
     *
     * @note Invalidates iterators at and after `pos` and all pointers if reallocation occurs.
     */
    template <class InputIt>
    constexpr auto insert(const_iterator pos, InputIt first, InputIt last) -> iterator
        requires std::input_iterator<InputIt> &&
                 std::constructible_from<T, typename std::iterator_traits<InputIt>::reference>
    {
        size_type index = pos - cbegin();

        if constexpr (std::forward_iterator<InputIt>)
        {
            auto count = static_cast<size_type>(std::distance(first, last));
            return insert_n(
                pos, count, [&](pointer dst) { copy_construct_range(dst, first, count); },
                [&](vector& temp)
                {
                    for (InputIt it = first; it != last; ++it)
                        temp.emplace_back(*it);
                });
        }
        else
        {
            size_type old_size = size_;

            try
            {
                for (; first != last; ++first)
                {
                    emplace_back(*first);
                }
            }
            catch (...)
            {
                // Restore strong guarantee for this phase: undo every element
                // appended so far. Original prefix [0, old_size) was never touched.
                while (size_ > old_size)
                {
                    pop_back();
                }
                throw;
            }

            // Move the newly appended block [old_size, size_) into position right
            // after `index`. `index` is still valid here even if reallocation
            // occurred during the loop above, since it's a stored offset, not a
            // pointer, and relative element order below old_size is unchanged.
            std::rotate(data_ + index, data_ + old_size, data_ + size_);

            return iterator(data_ + index);
        }
    }

    /**
     * @brief Inserts elements from an initializer list at the specified position
     *
     * Inserts elements from the initializer list `ilist` at the position indicated by `pos`.
     * Elements at and after `pos` are shifted to the right to make space for the new elements. If
     * the vector is at capacity, reallocation occurs automatically.
     *
     * @param pos A const_iterator indicating the position to insert before. Must be a valid
     * iterator into this vector.
     * @param ilist An initializer list of values to insert. Must be copy-constructible to T.
     * @return An iterator pointing to the first of the newly inserted elements or `pos` if the
     * initializer list is empty.
     *
     * @complexity \f$ O(\mbox{size} + \mbox{size of ilist}) \f$
     *
     * @exception std::length_error if size() + ilist.size() exceeds max_size()
     * @exception std::bad_alloc if reallocation needed but allocation fails
     * @exception ... Any exception thrown by T's copy/move constructor during shifting or
     * construction
     *
     * @exception_safety Strong exception safety: if insertion fails, the vector is unchanged.
     *
     * @note Invalidates iterators at and after `pos` and all pointers if reallocation occurs.
     */
    constexpr auto insert(const_iterator pos, std::initializer_list<T> ilist) -> iterator
    {
        return insert(pos, ilist.begin(), ilist.end());
    }

    /**
     * @brief Constructs an element in-place at the specified position
     *
     * Constructs a new element directly in allocated memory at the position indicated by `pos`,
     * avoiding a temporary copy/move. Elements at and after `pos` are shifted to the right to make
     * space for the new element. The constructor for T is called with the forwarded arguments. If
     * the vector is at capacity, automatic reallocation occurs. Returns an iterator to the newly
     * constructed element.
     *
     * @tparam Args Types of arguments to forward to T's constructor. The new element will be
     * constructed as T(std::forward<Args>(args)...).
     * @param pos A const_iterator indicating the position to insert before. Must be a valid
     * iterator into this vector.
     * @param args Arguments to forward to the constructor. Must be valid constructor arguments for
     * T.
     * @return An iterator pointing to the newly constructed element at the specified position.
     *
     * @complexity \f$ O(\mbox{size}) \f$
     *
     * @exception std::length_error if size() + 1 exceeds max_size()
     * @exception std::bad_alloc if reallocation needed but allocation fails
     * @exception ... Any exception thrown by T's constructor during construction or by move/copy
     * constructors during shifting
     *
     * @exception_safety Strong exception safety: if insertion fails, the vector is unchanged.
     *
     * @note More efficient than insert() when construction requires temporary creation. Example:
     * vec.emplace(pos, x, y, z) instead of vec.insert(pos, T(x, y, z))
     * @note Invalidates iterators at and after `pos` and all pointers if reallocation occurs.
     *
     * @see insert(const_iterator, const_reference) and insert(const_iterator, T&&) for simpler
     * element addition
     */
    template <class... Args> constexpr auto emplace(const_iterator pos, Args&&... args) -> iterator
    {
        return insert_n(
            pos, 1,
            [&](pointer dst) { traits::construct(alloc_, dst, std::forward<Args>(args)...); },
            [&](vector& temp) { temp.emplace_back(std::forward<Args>(args)...); });
    }

    /**
     * @brief Removes the last element from the vector
     *
     * Decrements size and calls the destructor on the removed element. Does nothing if the vector
     * is empty (no-op for safety). The removed element's destructor is called to clean up any
     * resources it holds.
     *
     * @complexity \f$ O(1) \f$ + destructor cost of T
     * @exception noexcept
     *
     * @note Invalidates the end() iterator and any pointers to the last element.
     * @note The capacity remains unchanged; use shrink_to_fit() if you want to release memory.
     *
     * @see push_back() to add elements
     * @see back() for accessing the last element
     */
    constexpr auto pop_back() noexcept -> void
    {
        if (size_ > 0) [[likely]]
        {
            --size_;

            if constexpr (!detail::trivially_destructible_v<value_type, Alloc>)
                traits::destroy(alloc_, data_ + size_);
        }
    }

    /**
     * @brief Removes the element at the specified position
     *
     * Removes the element at the position indicated by `pos`. Elements after `pos` are shifted to
     * the left to fill the gap.
     *
     * @param pos An iterator pointing to the element to remove. Must be a valid and dereferenceable
     * iterator into this vector.
     * @return An iterator pointing to the element that followed the removed element, or end() if
     * the removed element was the last one.
     *
     * @complexity \f$ O(\mbox{size} - (pos - begin())) \f$
     * @exception noexcept(std::is_nothrow_move_assignable_v<value_type>) if T's move assignment
     * operator is noexcept, otherwise may throw if T's move assignment can throw
     *
     * @exception_safety Basic exception safety: if removal fails, the vector remains in a valid
     * state, but elements may have been shifted.
     *
     * @note Invalidates iterators at and after `pos` and all pointers to elements after `pos`.
     * @see erase(const_iterator, const_iterator) to remove a range of elements
     */
    constexpr auto
    erase(const_iterator pos) noexcept(std::is_nothrow_move_assignable_v<value_type>) -> iterator
    {
        // Calculate raw index
        size_type index = pos - cbegin();
        assert(index < size_ && "forge::vector::erase: iterator is out of range");

        if constexpr (detail::trivially_manipulable_v<value_type, Alloc>)
        {
            // For trivially manipulable types, we can use memmove to shift the tail in one go.
            if !consteval
            {
                std::memmove(static_cast<void*>(data_ + index),
                             static_cast<const void*>(data_ + index + 1),
                             (size_ - index - 1) * sizeof(value_type));
                --size_;
                return iterator(data_ + index);
            }
        }

        // Shift elements after the removed element to the left
        for (size_type i = index + 1; i < size_; ++i)
        {
            data_[i - 1] = std::move_if_noexcept(data_[i]);
        }

        // Shrink the vector by one element and destroy the last element which is now a
        // duplicate/moved-from object after the shift
        --size_;
        if constexpr (!detail::trivially_destructible_v<value_type, Alloc>)
            traits::destroy(alloc_, data_ + size_);

        // Return an iterator pointing to the element that followed the removed element, or end() if
        // it was the last one
        return iterator(data_ + index);
    }

    /**
     * @brief Removes a range of elements from the vector
     *
     * Removes the elements in the range [first, last) from the vector. Elements after the removed
     * range are shifted to the left to fill the gap.
     *
     * @param first An iterator pointing to the first element to remove. Must be a valid iterator
     * into this vector.
     * @param last An iterator pointing to the end of the range to remove. Must be a valid iterator
     * into this vector.
     * @return An iterator pointing to the element that followed the removed range, or end() if the
     * removed range was at the end of the vector.
     *
     * @complexity \f$ O(\mbox{size} - (last - begin())) \f$
     * @exception noexcept(std::is_nothrow_move_assignable_v<value_type>) if T's move assignment
     * operator is noexcept, otherwise may throw if T's move assignment can throw
     *
     * @exception_safety Basic exception safety: if removal fails, the vector remains in a valid
     * state, but elements may have been shifted.
     *
     * @note Invalidates iterators at and after `first` and all pointers to elements after `first`.
     * @see erase(const_iterator) to remove a single element
     */
    constexpr auto
    erase(const_iterator first,
          const_iterator last) noexcept(std::is_nothrow_move_assignable_v<value_type>) -> iterator
    {
        size_type index_first = first - cbegin();

        if (first == last) [[unlikely]]
            return iterator(data_ + index_first); // No elements to erase, return iterator to first

        size_type index_last = last - cbegin();
        size_type count = index_last - index_first;

        if constexpr (detail::trivially_manipulable_v<T, Alloc>)
        {
            if !consteval
            {
                std::memmove(static_cast<void*>(data_ + index_first),
                             static_cast<const void*>(data_ + index_last),
                             (size_ - index_last) * sizeof(T));
                size_ -= count;
                return iterator(data_ + index_first);
            }
        }

        for (size_type i = index_last; i < size_; ++i)
        {
            data_[i - count] = std::move_if_noexcept(data_[i]);
        }

        size_ -= count;
        if constexpr (!detail::trivially_destructible_v<value_type, Alloc>)
        {
            for (size_type i = size_; i < size_ + count; ++i)
            {
                traits::destroy(alloc_, data_ + i);
            }
        }

        return iterator(data_ + index_first);
    }

    /**
     * @brief Changes the vector to contain exactly `new_size` elements
     *
     * If new_size > size(), inserts default-constructed elements at the end.
     * If new_size < size(), removes elements from the end.
     * If new_size == size(), this is a no-op.
     *
     * @param new_size The desired new size. Must not exceed max_size().
     *
     * @complexity \f$ O(|\mbox{new_size} - \mbox{size}|) \f$
     *
     * @exception std::length_error if new_size exceeds max_size()
     * @exception std::bad_alloc if capacity growth is needed but allocation fails
     * @exception ... Any exception thrown by T's default constructor when growing
     *
     * @exception_safety Strong exception safety: on exception, the vector remains unchanged.
     *
     * @warning When growing, newly added elements are default-constructed. For primitives,
     *          this means uninitialized values. Use resize(new_size, value) if you need
     *          specific initial values.
     *
     * @note Any iterators at or after position size() are invalidated by shrinking.
     *       All iterators are invalidated if capacity changes.
     *
     * @see resize(size_type, const_reference) to resize with specific fill value
     */
    constexpr auto resize(size_type new_size) -> void
    {
        if (new_size > max_size()) [[unlikely]]
            throw std::length_error("forge::vector::resize: new size exceeds max_size()");

        if (new_size < size_)
        {
            truncate_to(new_size);
        }
        else if (new_size > size_)
        {
            // Grow the vector, first ensure we have enough capacity for the new size
            if (new_size > capacity_)
            {
                reserve(new_size);
            }

            size_type count = new_size - size_;
            construct_fill_default(count);
            size_ = new_size;
        }
    }

    /**
     * @brief Changes the vector to contain exactly `new_size` elements with specified fill value
     *
     * If new_size > size(), copies `value` into each new element at the end.
     * If new_size < size(), removes elements from the end.
     * If new_size == size(), this is a no-op.
     *
     * @param new_size The desired new size. Must not exceed max_size().
     * @param value The value to fill new elements with if growing. Must be copy-constructible.
     *
     * @complexity \f$ O(|\mbox{new_size} - \mbox{size}|) \f$
     *
     * @exception std::length_error if new_size exceeds max_size()
     * @exception std::bad_alloc if capacity growth is needed but allocation fails
     * @exception ... Any exception thrown by T's copy constructor when growing
     *
     * @exception_safety Strong exception safety: on exception, the vector remains unchanged.
     *
     * @note Particularly useful for initializing a vector to a known size with a fill value,
     *       e.g., resize(100, 0) for a 100-element zero-filled array.
     * @note Any iterators at or after position size() are invalidated by shrinking.
     *       All iterators are invalidated if capacity changes.
     *
     * @see resize(size_type) for default-constructed element initialization
     */
    constexpr auto resize(size_type new_size, const_reference value) -> void
    {
        if (new_size > max_size()) [[unlikely]]
            throw std::length_error("forge::vector::resize: new size exceeds max_size()");

        if (new_size < size_)
        {
            truncate_to(new_size);
        }
        else if (new_size > size_)
        {
            // Copy `value` before reserve() in case `value` aliases an element
            // in the current buffer that would be freed/moved by the reallocation.
            const value_type fill_val(value);

            if (new_size > capacity_)
            {
                reserve(new_size);
            }

            size_type count = new_size - size_;
            construct_fill(data_ + size_, count, fill_val);
            size_ = new_size;
        }
    }

    /**
     * @brief Removes all elements from the vector, leaving it empty
     *
     * Destroys all contained elements by calling their destructors and sets size to zero.
     * After this operation, the vector is empty but has not released its allocated buffer.
     *
     * @complexity \f$ O(\mbox{size}) \f$ for non-trivially destructible types; \f$ O(1) \f$
     * for trivially destructible types.
     * @exception noexcept
     *
     * @note This is a very efficient way to empty a vector while keeping its allocated capacity
     *       for reuse. If you need to free memory, call shrink_to_fit() after clear().
     * @note All iterators are invalidated (they now point past the logical end).
     *
     * @see pop_back() to remove a single element
     * @see shrink_to_fit() to release excess capacity
     * @see ~vector() to completely destroy the vector
     */
    constexpr auto clear() noexcept -> void
    {
        truncate_to(0);
    }

    /**
     * @brief Replaces all vector elements with `count` copies of `value`
     *
     * Replaces the entire contents of the vector with `count` copies of the provided value.
     * Uses the copy-and-swap idiom for strong exception safety. The size becomes `count`;
     * capacity is set to `count` (no excess capacity is retained).
     *
     * @param count The desired number of copies. If 0, the vector becomes empty.
     * @param value The value to fill with. Must be copy-constructible.
     *
     * @complexity \f$ O(\mbox{count}) \f$
     * @exception std::bad_alloc if allocation fails
     * @exception ... Any exception thrown by T's copy constructor
     *
     * @exception_safety Strong exception safety: on exception, the original contents are preserved.
     *
     * @note All iterators and pointers are invalidated.
     * @note Useful for resetting a vector to a known state, e.g., assign(100, 0) for zeroing.
     *
     * @see resize(size_type, const_reference) to change size while keeping excess capacity
     */
    constexpr auto assign(size_type count, const_reference value) -> void
        requires std::copy_constructible<T>
    {
        // Create a temporary vector with desired size and content
        vector temp(count, value, alloc_);
        // Swap it into place
        this->swap(temp);
    }

    /**
     * @brief Replaces all vector elements with elements from a range [first, last)
     *
     * Replaces the entire contents of the vector with elements from the provided iterator range.
     * Uses copy-and-swap for strong exception safety. After assignment, the vector contains
     * copies of all elements in [first, last).
     *
     * @tparam InputIt The iterator type, must satisfy std::input_iterator concept.
     *
     * @param first Iterator to the first element in the source range.
     * @param last Iterator one past the last element in the source range.
     *
     * @complexity \f$ O(n) \f$ where n = std::distance(first, last)
     * @exception std::bad_alloc if allocation fails
     * @exception ... Any exception thrown by T's copy constructor
     *
     * @exception_safety Strong exception safety: on exception, the original contents are preserved.
     *
     * @pre [first, last) must be a valid range. Undefined behavior if not.
     * @pre The iterator range must not come from *this.
     *
     * @note All iterators and pointers are invalidated.
     */
    template <typename InputIt>
        requires std::input_iterator<InputIt>
    constexpr auto assign(InputIt first, InputIt last) -> void
    {
        // Create a temporary vector from iterator range
        vector temp(first, last, alloc_);

        // Swap it into place
        this->swap(temp);
    }

    /**
     * @brief Replaces all vector elements with elements from an initializer list
     *
     * Replaces the entire contents of the vector with elements from the provided initializer list.
     * After assignment, the vector contains copies of all elements from the initializer list.
     * Uses copy-and-swap for strong exception safety.
     *
     * @param init The initializer list providing elements to assign.
     *
     * @complexity \f$ O(\mbox{size of init}) \f$
     * @exception std::bad_alloc if allocation fails
     * @exception ... Any exception thrown by T's copy constructor
     *
     * @exception_safety Strong exception safety: on exception, the original contents are preserved.
     *
     * @note All iterators and pointers are invalidated.
     * @note Enables convenient reassignment with brace syntax: v.assign({1, 2, 3, 4});
     */
    constexpr auto assign(std::initializer_list<T> init) -> void
    {
        assign(init.begin(), init.end());
    }

    /**
     * @brief Exchanges the contents of this vector with another vector
     *
     * Efficiently swaps all internal state between two vectors: data pointer, size, and capacity.
     * The allocator is swapped only when `propagate_on_container_swap` is true; otherwise the
     * allocators must be equal and only the buffer pointers are exchanged.
     * After swap, the contents of this vector become those of `other`, and vice versa.
     *
     * @param other The other vector to swap with. Must be the same type and allocator.
     *
     * @complexity \f$ O(1) \f$
     * @exception noexcept if `propagate_on_container_swap` is false (pointer swaps only) or
     *            if `propagate_on_container_swap` is true and Alloc is nothrow-swappable.
     *
     * @note If `propagate_on_container_swap::value` is `false` and the two allocators are not
     *       equal, calling swap() is undefined behavior. A debug assertion is provided to catch
     * this in non-release builds.
     * @note Iterators remain valid but now refer to elements in the other vector.
     * @note This is the underlying operation used by copy-and-swap idiom for exception safety.
     *
     * @see non-member swap(vector&, vector&) for the standard free function
     */
    constexpr auto swap(vector& other) noexcept(!traits::propagate_on_container_swap::value ||
                                                std::is_nothrow_swappable_v<Alloc>) -> void
    {
        using std::swap;
        if constexpr (traits::propagate_on_container_swap::value)
        {
            swap(alloc_, other.alloc_);
        }
        else
        {
            // if POCS is false and allocators differ, swap is UB.
            assert(alloc_ == other.alloc_ && "forge::vector::swap: UB - allocators are unequal and "
                                             "propagate_on_container_swap is false");
        }
        swap(data_, other.data_);
        swap(size_, other.size_);
        swap(capacity_, other.capacity_);
    }

  public:
    // =========================================================
    // Lookup
    // =========================================================

    /**
     * @brief Checks if the vector contains a specific value
     *
     * Searches the vector for an element equal to the provided value. Returns true if found,
     * false otherwise. Uses operator== for equality comparison.
     *
     * @tparam U The type of the value to search for. Must be equality-comparable with T.
     * @param value The value to search for.
     * @return true if the vector contains an element equal to value; false otherwise.
     *
     * @complexity \f$ O(n) \f$
     * @exception noexcept if the comparison between T and U is noexcept.
     *
     * @see find(const_reference) for retrieving an iterator to the found element
     */
    template <typename U = T>
        requires std::equality_comparable_with<T, U>
    [[nodiscard]] constexpr auto contains(const U& value) const
        noexcept(noexcept(std::declval<const T&>() == value)) -> bool
    {
        return std::ranges::find(data_, data_ + size_, value) != (data_ + size_);
    }

    /**
     * @brief Finds the first occurrence of a specific value and returns an iterator
     *
     * Performs a linear search for the first element equal to the provided value.
     *
     * @tparam U The type of the value to search for. Must be equality-comparable with T.
     * @param self The deduced instance of the vector (handles cv-ref qualifiers automatically).
     * @param value The value to search for.
     * @return An iterator to the first matching element, or end() if not found. The iterator
     * type matches the constness of the calling vector.
     *
     * @complexity \f$ O(n) \f$
     * @exception noexcept if the comparison between T and U is noexcept.
     *
     * @see contains(const_reference) for just checking existence without an iterator
     */
    template <typename U = T>
        requires std::equality_comparable_with<T, U>
    [[nodiscard]] constexpr auto find(this auto&& self, const_reference value) noexcept(
        noexcept(std::declval<const T&>() == value)) -> decltype(auto)
    {
        auto* ptr = std::ranges::find(self.data_, self.data_ + self.size_, value);

        using iterator_type =
            std::conditional_t<std::is_const_v<std::remove_reference_t<decltype(self)>>,
                               typename std::remove_reference_t<decltype(self)>::const_iterator,
                               typename std::remove_reference_t<decltype(self)>::iterator>;

        return iterator_type(ptr);
    }

  public:
    // =========================================================
    // Iterators
    // =========================================================

    /**
     * @brief A random-access iterator over vector elements
     *
     * Provides full random-access iterator semantics with contiguous iterator support. Acts as a
     * thin wrapper around a raw pointer (T*), maintaining pointer-like performance while providing
     * proper iterator semantics. Supports the full range of operations including arithmetic,
     * comparison, and dereferencing.
     *
     * This template is instantiated with T for mutable iterators and const T for const iterators.
     * Implicit conversions from mutable to const iterators are supported via the copy constructor.
     *
     * @tparam U The element type, either T (mutable) or const T (const iterator).
     *
     * @note All operations are noexcept as they involve only pointer arithmetic.
     * @note Iterator validity is tied to vector reallocation: any reserve(), push_back(),
     *       insert(), erase(), or resize() that changes capacity() invalidates all iterators.
     *
     * @see forge::vector for the container using this iterator
     */
    template <typename U> class base_iterator
    {
      public:
        // =========================================================
        // Type aliases
        // =========================================================
        using iterator_concept = std::contiguous_iterator_tag;
        using iterator_category = std::random_access_iterator_tag;
        using value_type = U;
        using difference_type = std::ptrdiff_t;
        using pointer = U*;
        using reference = U&;

        // =========================================================
        // Constructors
        // =========================================================
        /**
         * @brief Default constructs an iterator pointing to nullptr
         *
         * Creates an iterator that does not point to any valid element. Used as a sentinel value
         * or placeholder.
         *
         * @complexity \f$ O(1) \f$
         * @exception noexcept
         *
         * @warning Do not dereference a default-constructed iterator.
         */
        constexpr base_iterator() noexcept : ptr_(nullptr) {}

        /**
         * @brief Constructs an iterator pointing to a specific memory location
         *
         * Creates an iterator that points to the provided address. Used internally to create
         * iterators from vector methods like begin(), end(), etc.
         *
         * @param ptr A pointer to the element the iterator should point to.
         *            May be nullptr for a past-the-end iterator (depending on context).
         *
         * @complexity \f$ O(1) \f$
         * @exception noexcept
         */
        constexpr base_iterator(pointer ptr) noexcept : ptr_(ptr) {}

        /**
         * @brief Implicit conversion constructor from mutable to const iterator
         *
         * Enables automatic conversion from base_iterator<T> to base_iterator<const T>.
         * This allows passing mutable iterators where const iterators are expected.
         * Only enabled when U is const and V is its non-const version.
         *
         * @tparam V The non-const version of U. For example, if U is const int, V must be int.
         * @param other A mutable iterator to convert from.
         *
         * @complexity \f$ O(1) \f$
         * @exception noexcept
         *
         * @note This conversion is implicit, matching standard iterator semantics.
         */
        template <typename V>
            requires(std::is_const_v<U> && std::is_same_v<std::remove_const_t<U>, V>)
        constexpr base_iterator(const base_iterator<V>& other) noexcept : ptr_(other.ptr_)
        {
        }

        // =========================================================
        // Derefrencing
        // =========================================================
        /**
         * @brief Dereferences the iterator to access the pointed-to element
         *
         * Returns a reference to the element the iterator points to. Dereferencing a past-the-end
         * iterator or a default-constructed iterator results in undefined behavior.
         *
         * @return A reference to the element.
         *
         * @complexity \f$ O(1) \f$
         * @exception noexcept
         *
         * @pre The iterator must be valid and dereferenceable (not past-the-end).
         *
         * @see operator->() to access members of the element
         * @see operator[](difference_type) for offset dereference
         */
        constexpr auto operator*() const noexcept -> reference
        {
            return *ptr_;
        }

        /**
         * @brief Accesses a member of the pointed-to element via the arrow operator
         *
         * Equivalent to (*it).member, allowing direct member access syntax. Useful when iterating
         * over containers of complex objects. The `const` qualifier here applies to the iterator
         * itself, not the element - `pointer` for a mutable `base_iterator<T>` is `T*`, which
         * still allows modification of the element.
         *
         * @return A pointer to the element for member access.
         *
         * @complexity \f$ O(1) \f$
         * @exception noexcept
         *
         * @pre The iterator must be valid and dereferenceable (not past-the-end).
         *
         * @example
         * @code
         * for (auto it = vec.begin(); it != vec.end(); ++it)
         *     it->member = value;  // Uses operator->()
         * @endcode
         *
         * @see operator*() for dereferencing
         */
        constexpr auto operator->() const noexcept -> pointer
        {
            return ptr_;
        }

        /**
         * @brief Accesses an element at an offset from the current position
         *
         * Provides array-like indexing relative to the iterator's current position. Equivalent to
         * *(it + off). Allows both positive and negative offsets for random-access iteration.
         *
         * @param off The offset from the current position. Can be positive, negative, or zero.
         * @return A reference to the element at position (current + off).
         *
         * @complexity \f$ O(1) \f$
         * @exception noexcept
         *
         * @pre (ptr_ + off) must point to a valid element within the vector.
         *
         * @example
         * @code
         * auto it = vec.begin();
         * auto second_elem = it[1];    // Same as vec[1]
         * auto prev_elem = (vec.end())[−1];  // Last element
         * @endcode
         */
        constexpr auto operator[](difference_type off) const noexcept -> reference
        {
            return *(ptr_ + off);
        }

        // =========================================================
        // Increment / Decrement
        // =========================================================
        /**
         * @brief Pre-increment: advances to the next element and returns the updated iterator
         *
         * Moves the iterator forward by one position and returns a reference to the updated
         * iterator. This is the efficient form (preferred for loops).
         *
         * @return A reference to this iterator after incrementing.
         *
         * @complexity \f$ O(1) \f$
         * @exception noexcept
         *
         * @pre Must not be at or past end().
         *
         * @see operator+=(difference_type) for larger advances
         */
        constexpr auto operator++() noexcept -> base_iterator&
        { // Pre-increment
            ++ptr_;
            return *this;
        }

        /**
         * @brief Post-increment: advances to the next element, returns a copy of the old iterator
         *
         * Moves the iterator forward by one position, but returns a copy of the iterator before
         * the increment. Less efficient than pre-increment due to the copy.
         *
         * @return A copy of this iterator before incrementing.
         *
         * @complexity \f$ O(1) \f$
         * @exception noexcept
         *
         * @pre Must not be at or past end().
         *
         * @see operator+=(difference_type) for larger advances
         */
        constexpr auto operator++(int) noexcept -> base_iterator
        { // Post-increment
            base_iterator temp = *this;
            ++(*this);
            return temp;
        }

        /**
         * @brief Pre-decrement: moves to the previous element and returns the updated iterator
         *
         * Moves the iterator backward by one position and returns a reference to the updated
         * iterator. This is the efficient form (preferred).
         *
         * @return A reference to this iterator after decrementing.
         *
         * @complexity \f$ O(1) \f$
         * @exception noexcept
         *
         * @pre Must not be at or before begin().
         *
         * @see operator-=(difference_type) for larger backward moves
         */
        constexpr auto operator--() noexcept -> base_iterator&
        { // Pre-decrement
            --ptr_;
            return *this;
        }

        /**
         * @brief Post-decrement: moves to the previous element, returns a copy of the old iterator
         *
         * Moves the iterator backward by one position, but returns a copy of the iterator before
         * the decrement. Less efficient than pre-decrement due to the copy.
         *
         * @return A copy of this iterator before decrementing.
         *
         * @complexity \f$ O(1) \f$ - pointer arithmetic + copy
         * @exception noexcept
         *
         * @pre Must not be at or before begin().
         *
         * @see operator-=(difference_type) for larger backward moves
         */
        constexpr auto operator--(int) noexcept -> base_iterator
        { // Post-decrement
            base_iterator temp = *this;
            --(*this);
            return temp;
        }

        // =========================================================
        // Arithmetic
        // =========================================================
        /**
         * @brief Advances the iterator by the specified number of positions
         *
         * Moves the iterator forward (or backward if off is negative) by the specified offset.
         * Equivalent to ptr_ += off.
         *
         * @param off The number of positions to advance. Positive moves forward, negative backward.
         * @return A reference to this iterator after advancing.
         *
         * @complexity \f$ O(1) \f$
         * @exception noexcept
         *
         * @pre The resulting position must be within the vector's valid range [data_, data_+size_].
         *
         * @see operator+(difference_type) for creating a new iterator
         * @see operator++() for single-step increment
         */
        constexpr auto operator+=(difference_type off) noexcept -> base_iterator&
        {
            ptr_ += off;
            return *this;
        }

        /**
         * @brief Moves the iterator backward by the specified number of positions
         *
         * Moves the iterator backward (or forward if off is negative) by the specified offset.
         * Equivalent to ptr_ -= off.
         *
         * @param off The number of positions to move backward. Positive moves backward, negative
         *            forward.
         * @return A reference to this iterator after moving.
         *
         * @complexity \f$ O(1) \f$
         * @exception noexcept
         *
         * @pre The resulting position must be within the vector's valid range [data_, data_+size_].
         *
         * @see operator-(difference_type) for creating a new iterator
         * @see operator--() for single-step decrement
         */
        constexpr auto operator-=(difference_type off) noexcept -> base_iterator&
        {
            ptr_ -= off;
            return *this;
        }

        /**
         * @brief Returns a new iterator advanced by the specified number of positions
         *
         * Creates and returns a new iterator that is `off` positions ahead of this iterator.
         * Does not modify this iterator.
         *
         * @param it The iterator to advance from.
         * @param off The number of positions to advance.
         * @return A new iterator pointing to (it.ptr_ + off).
         *
         * @complexity \f$ O(1) \f$
         * @exception noexcept
         *
         * @see operator+=(difference_type) for in-place advance
         */
        constexpr friend auto operator+(base_iterator it,
                                        difference_type off) noexcept -> base_iterator
        {
            it += off;
            return it;
        }

        /**
         * @brief Returns a new iterator advanced by the specified number of positions
         *
         * Creates and returns a new iterator that is `off` positions ahead of the iterator.
         * Does not modify the original iterator.
         *
         * @param off The number of positions to advance.
         * @param it The iterator to advance from.
         * @return A new iterator pointing to (it.ptr_ + off).
         *
         * @complexity \f$ O(1) \f$
         * @exception noexcept
         *
         * @see operator+=(difference_type) for in-place advance
         */
        constexpr friend auto operator+(difference_type off,
                                        base_iterator it) noexcept -> base_iterator
        {
            it += off;
            return it;
        }

        /**
         * @brief Returns a new iterator moved backward by the specified number of positions
         *
         * Creates and returns a new iterator that is `off` positions behind this iterator.
         * Does not modify this iterator.
         *
         * @param it The iterator to move backward from.
         * @param off The number of positions to move backward.
         * @return A new iterator pointing to (it.ptr_ - off).
         *
         * @complexity \f$ O(1) \f$
         * @exception noexcept
         *
         * @see operator-=(difference_type) for in-place backward move
         */
        constexpr friend auto operator-(base_iterator it,
                                        difference_type off) noexcept -> base_iterator
        {
            it -= off;
            return it;
        }

        /**
         * @brief Calculates the distance between two iterators
         *
         * Returns the number of elements between two iterators. Positive if lhs is ahead of rhs,
         * negative if behind, zero if equal.
         *
         * @param lhs The left-hand iterator.
         * @param rhs The right-hand iterator (the subtrahend).
         * @return The difference: (lhs.ptr_ - rhs.ptr_), as a signed integer.
         *
         * @complexity \f$ O(1) \f$
         * @exception noexcept
         *
         * @note Both iterators should point into the same vector for meaningful results.
         *
         * @example
         * @code
         * auto it1 = vec.begin();
         * auto it2 = vec.begin() + 5;
         * auto distance = it2 - it1;  // Returns 5
         * @endcode
         */
        constexpr friend auto operator-(const base_iterator& lhs,
                                        const base_iterator& rhs) noexcept -> difference_type
        {
            return lhs.ptr_ - rhs.ptr_;
        }

        // =========================================================
        // Comparison
        // =========================================================
        /**
         * @brief Compares two iterators for equality and ordering
         *
         * Uses the default three-way comparison of the underlying pointers. Allows all comparison
         * operators (==, !=, <, <=, >, >=) to work automatically.
         *
         * @param other The iterator to compare with.
         * @return The comparison result of the underlying pointers.
         *
         * @complexity \f$ O(1) \f$
         * @exception noexcept
         *
         * @note The default implementation compares ptr_ with other.ptr_ for all relationships.
         */
        constexpr auto operator<=>(const base_iterator&) const noexcept = default;

      private:
        pointer ptr_;

        template <typename> friend class base_iterator;
    }; // class base_iterator

    /**
     * @brief Returns an iterator to the first element
     *
     * Returns an iterator (mutable on a non-const vector, const on a const vector) to the first
     * element. If the vector is empty, the returned iterator equals end().
     *
     * @param self The deduced instance of the vector.
     * @return `iterator` on a non-const vector, `const_iterator` on a const vector.
     *         Returns end() if the vector is empty.
     *
     * @complexity \f$ O(1) \f$
     * @exception noexcept
     *
     * @note Invalidated by any operation that causes reallocation (reserve, push_back, insert,
     * etc.)
     *
     * @see end() for the past-the-end iterator
     * @see cbegin() for a const version
     */
    [[nodiscard]] constexpr auto begin(this auto& self) noexcept
    {
        using it_t = std::conditional_t<std::is_const_v<std::remove_reference_t<decltype(self)>>,
                                        const_iterator, iterator>;

        return it_t(self.data_);
    }

    /**
     * @brief Returns a const iterator to the first element (explicit const version)
     *
     * Explicitly returns a const iterator even on non-const vectors. Equivalent to begin() const
     * when called on a const vector, but allows explicit const iteration on mutable vectors.
     *
     * @return A const iterator to the first element (or cend() if empty).
     *
     * @complexity \f$ O(1) \f$
     * @exception noexcept
     *
     * @note Use when you explicitly need const semantics even on mutable vectors.
     * @note Invalidated by any operation that causes reallocation (reserve, push_back, insert,
     * etc.).
     *
     * @see cend() for the past-the-end variant
     * @see begin() for the non-const version
     */
    [[nodiscard]] constexpr auto cbegin() const noexcept -> const_iterator
    {
        return const_iterator(data_);
    }

    /**
     * @brief Returns an iterator to one past the last element (the end sentinel)
     *
     * Returns an iterator pointing to the position one past the last valid element. This iterator
     * is used as a sentinel and should not be dereferenced. It is equal to begin() if the vector
     * is empty.
     *
     * @param self The deduced instance of the vector.
     * @return `iterator` on a non-const vector, `const_iterator` on a const vector.
     *
     * @complexity \f$ O(1) \f$
     * @exception noexcept
     *
     * @pre Do not dereference the returned iterator.
     *
     * @note Invalidated by any operation that causes reallocation (reserve, push_back, insert,
     * etc.).
     *
     * @see begin() for the first element iterator
     * @see cend() for the const version
     */
    [[nodiscard]] constexpr auto end(this auto& self) noexcept
    {
        using it_t = std::conditional_t<std::is_const_v<std::remove_reference_t<decltype(self)>>,
                                        const_iterator, iterator>;

        return it_t(self.data_ + self.size_);
    }

    /**
     * @brief Returns a const iterator to one past the last element (explicit const version)
     *
     * Explicitly returns a const iterator end sentinel even on non-const vectors. Equivalent to
     * end() const when called on a const vector, but allows explicit const iteration on mutable
     * vectors.
     *
     * @return A const iterator to one past the last element.
     *
     * @complexity \f$ O(1) \f$
     * @exception noexcept
     *
     * @pre Do not dereference the returned iterator.
     *
     * @note Use when you explicitly need const semantics even on mutable vectors.
     * @note Invalidated by any operation that causes reallocation (reserve, push_back, insert,
     * etc.).
     *
     * @see cbegin() for the past-the-beginning variant
     * @see end() for the non-const version
     */
    [[nodiscard]] constexpr auto cend() const noexcept -> const_iterator
    {
        return const_iterator(data_ + size_);
    }

  private:
    // =========================================================
    // Helper functions
    // =========================================================

    /**
     * @brief Computes the next capacity using the 1.5x growth strategy
     *
     * Calculates the new buffer size to use when the vector must grow, applying a 1.5x
     * multiplicative factor. Handles the edge case where capacity_ is 0 (returns 1) and
     * where integer rounding causes `capacity_ + capacity_/2 == capacity_` for small values
     * (guards by returning `capacity_ + 1` in that case).
     *
     * @return The next capacity to allocate.
     *
     * @complexity \f$ O(1) \f$
     * @exception std::length_error if the computed capacity exceeds max_size()
     *
     * @note The 1.5x growth factor strikes a balance between avoiding frequent reallocations
     *       and excessive memory waste compared to a 2x factor.
     *
     * @see reallocate() which uses this helper
     * @see emplace_back_realloc() which uses this helper
     */
    [[nodiscard]] constexpr auto compute_next_capacity() const -> size_type
    {
        size_type new_cap = (capacity_ == 0) ? 1 : capacity_ + capacity_ / 2;

        // Guard against integer rounding: for capacity_ == 1, capacity_ + capacity_/2 == 1
        // (no growth). Bump to capacity_ + 1 so we always make forward progress.
        if (new_cap <= capacity_)
            new_cap = capacity_ + 1;

        if (new_cap > max_size()) [[unlikely]]
            throw std::length_error("forge::vector: size exceeds max_size()");

        return new_cap;
    }

    /**
     * @brief Reallocates the internal buffer using 1.5x growth factor
     *
     * Internal helper called when the vector needs more capacity in contexts that do NOT
     * require self-insertion safety (e.g. operations that have already materialized their
     * values). Delegates to `reserve(compute_next_capacity())`.
     *
     * @complexity \f$ O(\mbox{size}) \f$
     *
     * @exception std::length_error if the new capacity exceeds max_size()
     * @exception std::bad_alloc if allocation fails
     * @exception ... Any exception thrown by T's move/copy constructor
     *
     * @exception_safety Strong: on exception, vector is unchanged
     *
     * @note For `emplace_back`, use `emplace_back_realloc` instead, which constructs the
     *       new element *before* releasing the old buffer to guard against self-insertion.
     *
     * @see reserve(size_type) for requesting a specific capacity
     * @see emplace_back_realloc for the self-insertion-safe growth path
     */
    constexpr auto reallocate() -> void
    {
        reserve(compute_next_capacity());
    }

    /**
     * @brief Self-insertion-safe reallocation helper for emplace_back
     *
     * Called by `emplace_back` when the vector is at capacity. Unlike `reallocate()` followed
     * by `traits::construct(...)`, this helper constructs the new element into the freshly
     * allocated buffer *before* the old buffer is released, ensuring that arguments which alias
     * elements of the current container (e.g. `vec.emplace_back(vec[0])`) remain valid during
     * construction.
     *
     * The sequence is:
     *  1. Compute new capacity and allocate a fresh buffer.
     *  2. Construct the new element at position `size_` in the new buffer (old buffer still live).
     *  3. Relocate existing elements `[0, size_)` to the new buffer.
     *  4. Destroy old elements (non-trivial types) and release the old buffer.
     *  5. Update `data_`, `capacity_`, and `size_`.
     *
     * @tparam Args Types of arguments to forward to T's constructor.
     * @param args Arguments to forward. May safely alias elements of the current buffer.
     * @return A reference to the newly constructed element.
     *
     * @complexity \f$ O(\mbox{size}) \f$
     *
     * @exception std::length_error if the computed capacity exceeds max_size()
     * @exception std::bad_alloc if allocation fails
     * @exception ... Any exception thrown by T's constructor or by T's move/copy constructor
     *              during relocation
     *
     * @exception_safety Strong: on any exception the vector is left unchanged and all
     *                   partially-constructed elements in the new buffer are properly destroyed.
     *
     * @see emplace_back for the public entry point
     */
    template <typename... Args>
    constexpr auto emplace_back_realloc(Args&&... args) -> reference
        requires std::constructible_from<T, Args...>
    {
        const size_type new_cap = compute_next_capacity();
        pointer new_data = traits::allocate(alloc_, new_cap);

        // Step 1 — Construct the new element at its final position WHILE the old buffer is
        // still live. This is what makes self-insertion (e.g. vec.emplace_back(vec[0])) safe:
        // `args` are still valid pointers/references into the old allocation.
        try
        {
            traits::construct(alloc_, new_data + size_, std::forward<Args>(args)...);
        }
        catch (...)
        {
            traits::deallocate(alloc_, new_data, new_cap);
            throw;
        }

        // Step 2 — Relocate existing elements [0, size_) into the new buffer.
        try
        {
            relocate_elements(new_data);
        }
        catch (...)
        {
            // Must destroy the already-constructed new element before freeing the new buffer.
            traits::destroy(alloc_, new_data + size_);
            traits::deallocate(alloc_, new_data, new_cap);
            throw;
        }

        // Step 3 — Release the old buffer. For non-trivial types, `relocate_elements` used
        // move/copy construction, so the old objects must be explicitly destroyed first.
        if constexpr (!detail::trivially_manipulable_v<value_type, Alloc>)
        {
            for (size_type j = 0; j < size_; ++j)
                traits::destroy(alloc_, data_ + j);
        }
        if (data_)
            traits::deallocate(alloc_, data_, capacity_);

        data_ = new_data;
        capacity_ = new_cap;
        return data_[size_++];
    }

    /**
     * @brief Fills `count` elements at `dst` by copy-constructing from `value`
     *
     * Constructs `count` copies of `value` into the uninitialized storage at `dst`. Uses a fast
     * path (memset for 1-byte trivial types, std::fill_n for larger trivial types) when the type
     * and allocator permit, and falls back to `traits::construct` with strong exception safety
     * for non-trivial types.
     *
     * @param dst   Pointer to the first byte of uninitialized storage. Must have room for at
     *              least `count` elements of type T.
     * @param count Number of elements to construct.
     * @param value Source value to copy from. Must remain valid for the entire call.
     *
     * @complexity \f$ O(\mbox{count}) \f$
     *
     * @exception ... Any exception thrown by T's copy constructor.
     *
     * @exception_safety Strong: if any construction fails, all previously constructed elements
     *                   in `[dst, dst+i)` are destroyed before the exception is re-thrown.
     *
     * @note For trivially copyable single-byte types the entire fill is reduced to a single
     *       `std::memset` call outside constant evaluation contexts.
     */
    constexpr auto construct_fill(pointer dst, size_type count, const_reference value) -> void
    {
        if constexpr (detail::trivially_manipulable_v<value_type, Alloc>)
        {
            if !consteval
            {
                if constexpr (sizeof(value_type) == 1)
                    std::memset(dst, static_cast<unsigned char>(value), count);
                else
                    std::fill_n(dst, count, value);
                return;
            }
        }

        size_type i = 0;
        try
        {
            for (; i < count; ++i)
                traits::construct(alloc_, dst + i, value);
        }
        catch (...)
        {
            while (i > 0)
                traits::destroy(alloc_, dst + (--i));
            throw;
        }
    }

    /**
     * @brief Default-constructs `count` elements into the uninitialized storage at `dst`
     *
     * Constructs `count` elements into `[dst, dst + count)` using `traits::construct(alloc_, p)`
     * (value-initialization). For trivially default-constructible types with a standard allocator,
     * construction is skipped entirely outside of constant-evaluation contexts (the memory is left
     * with indeterminate values) — matching the `resize(count)` performance contract documented in
     * the class header.
     *
     * @param dst   Pointer to the first byte of uninitialized storage. Must have room for at
     *              least `count` elements of type T.
     * @param count Number of elements to default-construct.
     *
     * @complexity \f$ O(\mbox{count}) \f$ for non-trivial types; \f$ O(1) \f$ for trivially
     *             default-constructible types outside constant evaluation.
     *
     * @exception ... Any exception thrown by T's default constructor.
     *
     * @exception_safety Strong: if any construction fails, all previously constructed elements
     *                   in `[dst, dst+i)` are destroyed before the exception is re-thrown.
     *
     * @warning For trivially default-constructible types at runtime, newly created elements will
     *          contain indeterminate values. This is intentional for performance. If you need
     *          zero-initialization, use `construct_fill(dst, count, T{})` instead.
     */
    constexpr void construct_fill_default(size_type count)
    {
        if constexpr (detail::trivially_value_initializable_v<T, Alloc>)
        {
            if !consteval
            {
                // For trivially default-constructible types at runtime, skip construction
                // for performance (elements will contain indeterminate values).
                return;
            }
        }

        size_type i = 0;
        try
        {
            for (; i < count; ++i)
                traits::construct(alloc_, data_ + i);
        }
        catch (...)
        {
            while (i > 0)
                traits::destroy(alloc_, data_ + (--i));
            throw;
        }
    }

    /**
     * @brief Copy-constructs `n` elements from the range `[first, first+n)` into `dst`
     *
     * Constructs `n` copies of the elements in the range `[first, first+n)` into the
     * uninitialized storage at `dst`. Uses a fast memcpy path for trivially copyable types
     * accessed via contiguous iterators, and falls back to `traits::construct` with strong
     * exception safety otherwise.
     *
     * @tparam InputIt Any iterator type. The memcpy fast path requires
     *                 `std::contiguous_iterator<InputIt>` in addition to trivial copyability.
     * @param dst   Pointer to the first byte of uninitialized destination storage.
     * @param first Iterator to the first source element.
     * @param n     Number of elements to copy-construct.
     *
     * @complexity \f$ O(n) \f$; effectively \f$ O(1) \f$ for trivially copyable types via memcpy.
     *
     * @exception ... Any exception thrown by T's copy constructor.
     *
     * @exception_safety Strong: if any construction fails, all previously constructed elements
     *                   in `[dst, dst+i)` are destroyed before the exception is re-thrown.
     */
    template <typename InputIt>
    constexpr void copy_construct_range(pointer dst, InputIt first, size_type n)
    {
        if constexpr (detail::trivially_manipulable_v<value_type, Alloc> &&
                      std::contiguous_iterator<InputIt>)
        {
            if !consteval
            {
                if (n > 0)
                    std::memcpy(static_cast<void*>(dst),
                                static_cast<const void*>(std::to_address(first)),
                                n * sizeof(value_type));
                return;
            }
        }

        size_type i = 0;
        try
        {
            for (; i < n; ++i, ++first)
                traits::construct(alloc_, dst + i, *first);
        }
        catch (...)
        {
            while (i > 0)
                traits::destroy(alloc_, dst + (--i));
            throw;
        }
    }

    /**
     * @brief Relocates all `size_` existing elements into a new, already-allocated buffer
     *
     * Constructs elements at `[dst, dst+size_)` by moving (via `std::move_if_noexcept`) or
     * copying from the current buffer `data_`. For trivially copyable types, falls back to
     * a single memcpy. Does NOT free the old buffer or update any member variables.
     *
     * @param dst Pointer to the start of an already-allocated destination buffer. Must have
     *            room for at least `size_` elements. Must not overlap with `data_`.
     *
     * @complexity \f$ O(\mbox{size}) \f$; effectively \f$ O(1) \f$ for trivially copyable types.
     *
     * @exception ... Any exception thrown by T's move/copy constructor.
     *
     * @exception_safety Strong: if any construction fails, all partially-constructed elements
     *                   at `[dst, dst+i)` are destroyed before the exception is re-thrown.
     *                   The source buffer `data_` is left intact.
     *
     * @note Callers are responsible for destroying the source elements and deallocating the
     *       source buffer after this call succeeds.
     *
     * @see reserve(size_type) for the primary user of this helper
     * @see emplace_back_realloc for the emplace_back-specific reallocation path
     */
    constexpr auto relocate_elements(pointer dst) -> void
    {
        if constexpr (detail::trivially_manipulable_v<value_type, Alloc>)
        {
            if !consteval
            {
                if (size_ > 0)
                    std::memcpy(static_cast<void*>(dst), static_cast<const void*>(data_),
                                size_ * sizeof(value_type));
                return;
            }
        }

        size_type i = 0;
        try
        {
            for (; i < size_; ++i)
                traits::construct(alloc_, dst + i, std::move_if_noexcept(data_[i]));
        }
        catch (...)
        {
            while (i > 0)
                traits::destroy(alloc_, dst + (--i));
            throw;
        }
    }

    /**
     * @brief Destroys elements from the end until `size_` equals `new_size`
     *
     * For non-trivially-destructible types, calls `traits::destroy` on elements from index
     * `size_-1` down to `new_size`, decrementing `size_` as it goes so the vector stays
     * in a consistent state even if a destructor throws. For trivially destructible types,
     * simply sets `size_ = new_size` in O(1) without any destructor calls.
     *
     * @param new_size The target size. Must satisfy `new_size <= size_`.
     *
     * @complexity \f$ O(\mbox{size} - \mbox{new_size}) \f$ for non-trivial types;
     *             \f$ O(1) \f$ for trivially destructible types.
     *
     * @exception noexcept
     *
     * @see clear() which calls truncate_to(0)
     * @see resize(size_type) which calls truncate_to(new_size) when shrinking
     */
    constexpr void truncate_to(size_type new_size)
    {
        if constexpr (!detail::trivially_destructible_v<T, Alloc>)
        {
            while (size_ > new_size)
            {
                --size_;
                traits::destroy(alloc_, data_ + size_);
            }
        }
        else
        {
            size_ = new_size;
        }
    }

    /**
     * @brief Core insertion primitive: shifts a tail, fills a gap, and maintains strong safety
     *
     * All single- and multi-element insertion operations (insert, emplace) delegate here.
     * The function handles two major scenarios:
     *
     *  **Reallocation path** (`size_ + count > capacity_`): A temporary vector is built with
     *  enough capacity, the prefix `[0, index)` is pushed in, `append_n` is called to push
     *  the new elements, then the suffix `[index, size_)` is appended. The temporary is then
     *  swapped in atomically. This path provides the strong guarantee unconditionally.
     *
     *  **In-place path** (`size_ + count <= capacity_`): Two sub-cases:
     *   - *Trivially manipulable T*: `std::copy_backward` shifts the tail, then `fill_gap`
     *     fills the vacated positions. Cannot throw; size is updated atomically.
     *   - *Non-trivial T*: Elements in the tail are move-constructed one at a time into the
     *     newly-extended region, and then `fill_gap` constructs the new elements into the gap.
     *     A careful catch block restores the original layout if either phase throws.
     *
     * @tparam FillGapFn Callable with signature `void(pointer dst)`. Constructs exactly `count`
     *                   elements starting at `dst`.
     * @tparam AppendFn  Callable with signature `void(vector& temp)`. Appends exactly `count`
     *                   new elements to `temp` via `emplace_back`.
     *
     * @param pos      Position before which elements are inserted. Must be in `[cbegin(), cend()]`.
     * @param count    Number of elements to insert.
     * @param fill_gap Fills the gap left by the tail shift for the in-place path.
     * @param append_n Fills the new elements for the reallocation path.
     *
     * @return An iterator to the first newly inserted element.
     *
     * @complexity \f$ O(\mbox{size} + \mbox{count}) \f$
     *
     * @exception std::length_error if size() + count exceeds max_size()
     * @exception std::bad_alloc if reallocation fails
     * @exception ... Any exception thrown by T's constructor or move constructor
     *
     * @exception_safety Strong on the reallocation path. Strong on the trivial in-place path.
     *                   Strong on the non-trivial in-place path if the shift loop fails;
     *                   strong if the fill_gap phase fails after a complete shift. Basic only
     *                   if fill_gap itself throws after a partial shift of a throwing-move T.
     *
     * @note `fill_gap` and `append_n` must insert exactly `count` elements. Inserting more or
     *       fewer produces undefined behavior.
     * @note For the in-place non-trivial path, `fill_gap` must not read from elements at or
     *       after the insertion point (they have been moved away).
     */
    template <typename FillGapFn, typename AppendFn>
    constexpr auto insert_n(const_iterator pos, size_type count, FillGapFn&& fill_gap,
                            AppendFn&& append_n) -> iterator
    {
        size_type index = pos - cbegin();

        if (count == 0)
            return iterator(data_ + index);

        if (size_ + count > max_size()) [[unlikely]]
            throw std::length_error("forge::vector::insert: size exceeds max_size()");

        if (size_ + count > capacity_)
        {
            size_type new_capacity = std::max(size_ + count, capacity_ + capacity_ / 2);
            vector temp(alloc_);
            temp.reserve(new_capacity);

            for (size_type i = 0; i < index; ++i)
                temp.push_back(std::move_if_noexcept(data_[i]));

            append_n(temp);

            for (size_type i = index; i < size_; ++i)
                temp.push_back(std::move_if_noexcept(data_[i]));

            this->swap(temp);
            return iterator(data_ + index);
        }

        // In-place: no reallocation needed.
        if constexpr (detail::trivially_manipulable_v<T, Alloc>)
        {
            if !consteval
            {
                std::copy_backward(data_ + index, data_ + size_, data_ + size_ + count);
                fill_gap(data_ + index); // bulk fast path inside; cannot throw here
                size_ += count;
                return iterator(data_ + index);
            }
        }

        size_type elements_moved = 0;
        try
        {
            for (size_type i = size_; i > index; --i)
            {
                traits::construct(alloc_, data_ + i + count - 1,
                                  std::move_if_noexcept(data_[i - 1]));
                ++elements_moved;
                traits::destroy(alloc_, data_ + i - 1);
            }

            fill_gap(data_ + index);
        }
        catch (...)
        {
            // restore_base, not `index`: the elements_moved already-shifted elements
            // are the topmost elements_moved of the original tail, so they belong
            // back at [size_ - elements_moved, ...) — this is only equal to `index`
            // when elements_moved is the *full* shift count (i.e. the shift loop
            // succeeded and fill_gap failed). Using `index` unconditionally here
            // (as in the original implementation) silently corrupts memory when the
            // shift loop itself fails partway through.
            size_type restore_base = size_ - elements_moved;
            for (size_type j = 0; j < elements_moved; ++j)
            {
                traits::construct(alloc_, data_ + restore_base + j,
                                  std::move_if_noexcept(data_[restore_base + count + j]));
                traits::destroy(alloc_, data_ + restore_base + count + j);
            }
            throw;
        }

        size_ += count;
        return iterator(data_ + index);
    }

  private:
    // =========================================================
    // Member variables
    // =========================================================
    using traits = std::allocator_traits<Alloc>;

    Alloc alloc_;
    pointer data_;
    size_type size_;
    size_type capacity_;

    /// \cond DOXYGEN_SHOULD_SKIP_THIS
    template <typename> friend struct detail::vector_tests_accessor;
    /// \endcond

}; // class vector

/**
 * @brief Non-member swap function for efficient vector swapping
 *
 * Swaps the contents of two vectors by calling the member swap() function of the left operand.
 * This provides the standard library semantics for using std::swap() with vectors.
 *
 * @tparam T The element type of the vectors.
 * @tparam Alloc The allocator type of the vectors.
 *
 * @param lhs The first vector (left-hand side). Will contain rhs's contents after the call.
 * @param rhs The second vector (right-hand side). Will contain lhs's contents after the call.
 *
 * @complexity \f$ O(1) \f$
 * @exception noexcept
 *
 * @note Enables the idiom: forge::swap(v1, v2) or using ADL: swap(v1, v2)
 * @note More efficient than copying: swap is O(1) while copy assignment is O(n).
 *
 * @see vector::swap(vector&) for the member function
 *
 * @relates forge::vector
 */
template <typename T, typename Alloc>
constexpr auto swap(vector<T, Alloc>& lhs, vector<T, Alloc>& rhs) noexcept -> void
{
    lhs.swap(rhs);
}

} // namespace forge