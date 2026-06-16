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
#include <concepts>
#include <cstddef>
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
}

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

    template <typename U>
    class base_iterator; // Forward declaration of the base iterator class to be used in the type
                         // aliases for iterator and const_iterator

    using value_type = T;
    using allocator_type = Alloc;
    using difference_type = std::ptrdiff_t;
    using reference = T&;
    using const_reference = const T&;
    using pointer = T*;
    using const_pointer = const T*;
    using size_type = std::size_t;
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
        {
            throw std::length_error("Count exceeds maximum size");
        }

        data_ = traits::allocate(alloc_, count);

        // Use a try-catch block to ensure that if an exception is thrown during element
        // construction, we properly clean up any already constructed elements and deallocate the
        // memory to prevent leaks
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
        {
            throw std::length_error("Count exceeds maximum size");
        }
        data_ = traits::allocate(alloc_, count);

        // Use a try-catch block to ensure that if an exception is thrown during element
        // construction, we properly clean up any already constructed elements and deallocate the
        // memory to prevent leaks
        try
        {
            for (; size_ < count; ++size_)
            {
                traits::construct(alloc_, data_ + size_, value);
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
     * @complexity \f$ O(n) \f$ where n = std::distance(first, last)
     * @exception std::bad_alloc if memory allocation fails
     * @exception ... Any exception thrown by T's copy/move constructor
     *
     * @exception_safety Strong exception safety: if any constructor throws, the vector remains
     *                    empty, and no resources are leaked.
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
        size_type n = static_cast<size_type>(std::distance(first, last));

        if (n > 0)
        {
            data_ = traits::allocate(alloc_, n);
            capacity_ = n;

            try
            {
                while (first != last)
                {
                    traits::construct(alloc_, data_ + size_, *first);
                    ++size_;
                    ++first;
                }
            }
            catch (...)
            {
                // If any constructor throws we clean up everything
                for (size_type i = 0; i < size_; ++i)
                {
                    traits::destroy(alloc_, data_ + i);
                }
                traits::deallocate(alloc_, data_, capacity_);

                // Set pointers to null so the destructor won't attempt to clean up memory that has
                // already been cleaned up
                data_ = nullptr;
                size_ = 0;
                capacity_ = 0;

                throw; // Rethrow the exception to propagate it to the caller
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
        : alloc_(alloc), size_(0), capacity_(init.size())
    {
        if (capacity_ > 0)
        {
            data_ = traits::allocate(alloc_, capacity_);

            try
            {
                for (const auto& item : init)
                {
                    traits::construct(alloc_, data_ + size_, item);
                    ++size_; // Only increment after a successful construction
                }
            }
            catch (...)
            {
                // If any constructor throws we clean up everything
                for (size_type i = 0; i < size_; ++i)
                {
                    traits::destroy(alloc_, data_ + i);
                }
                traits::deallocate(alloc_, data_, capacity_);

                // Reset members to a safe state before rethrowing
                data_ = nullptr;
                size_ = 0;
                capacity_ = 0;

                throw;
            }
        }
        else
        {
            // If the initializer list is empty, we just initialize to an empty state without
            // allocating memory
            data_ = nullptr;
        }
    }

    /**
     * @brief Copy constructor - creates a deep copy of another vector
     *
     * Constructs a new vector as a complete independent copy of `other`. All elements are copied
     * from the source vector, and new memory is allocated. The allocator is selected according to
     * allocator traits (POCCA - propagate on container copy assignment).
     * Both size and capacity of the new vector match the source.
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
        : alloc_(traits::select_on_container_copy_construction(other.alloc_)), data_(nullptr),
          size_(0), capacity_(other.size_)
    {
        if (other.capacity_ > 0)
        {
            data_ = traits::allocate(alloc_, capacity_);

            try
            {
                for (; size_ < other.size_; ++size_)
                {
                    traits::construct(alloc_, data_ + size_, other.data_[size_]);
                }
            }
            catch (...)
            {
                // If any constructor throws we clean up everything
                for (size_type i = 0; i < size_; ++i)
                {
                    traits::destroy(alloc_, data_ + i);
                }
                traits::deallocate(alloc_, data_, capacity_);

                // Set pointers to null so the destructor won't attempt to clean up memory that has
                // already been cleaned up
                data_ = nullptr;
                size_ = 0;
                capacity_ = 0;

                throw; // Rethrow the exception to propagate it to the caller
            }
        }
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
        : alloc_(std::move(other.alloc_)), data_(other.data_), size_(other.size_),
          capacity_(other.capacity_)
    {
        other.data_ =
            nullptr; // Nullify the source vector's data pointer to prevent double deletion
        other.size_ = 0;
        other.capacity_ = 0;
    }

    /**
     * @brief Destructor - frees all resources
     *
     * Destroys all contained elements by calling their destructors, then deallocates the
     * underlying memory through the allocator. If the vector was never allocated (size and
     * capacity both zero), this is a no-op.
     *
     * @complexity \f$ O(\mbox{size}) \f$
     */
    constexpr ~vector() noexcept
    {
        if (data_)
        {
            clear();
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
    constexpr vector<T, Alloc>& operator=(const vector& other)
    {
        if (this != &other)
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
     * @brief Move assignment operator - efficiently transfers ownership
     *
     * Efficiently replaces the contents of this vector by taking ownership of resources from
     * `other`. The source vector is left in a valid but empty state. This uses the
     * swap operation for maximum efficiency.
     *
     * @param other The source vector to move from. Will be left in a valid but empty state.
     * @return A reference to *this, allowing for chaining assignments.
     *
     * @complexity \f$ O(1) \f$
     * @exception noexcept(std::is_nothrow_swappable_v<Alloc>)
     *
     * @note The source vector can be safely destroyed or reused after the operation.
     * @note Allocators are swapped (not moved), so resource ownership is completely transferred.
     *
     * @see swap(vector&) for the underlying operation
     */
    constexpr vector<T, Alloc>& operator=(vector&& other) noexcept(std::is_nothrow_swappable_v<Alloc>)
    {
        if (this != &other)
        {
            this->swap(other); // Swap the contents of the source vector with the current vector. This
                               // efficiently transfers ownership of the resources and leaves the source
                              // vector in a valid but unspecified state.
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
     * Allows efficient direct access to elements using operator[] syntax, like built-in arrays.
     * No bounds checking is performed, making this the fastest access method. The caller is
     * responsible for ensuring the index is within [0, size()).
     *
     * @param index The zero-based index of the element to access.
     *              Must be less than size(); behavior is undefined otherwise.
     * @return A reference to the element at the specified index, allowing reads and modifications.
     *
     * @complexity \f$ O(1) \f$
     * @exception noexcept
     *
     * @note For safety-critical code, use at(size_type) instead, which performs bounds checking.
     *
     * @see at(size_type) for bounds-checked access
     */
    [[nodiscard]] constexpr reference operator[](size_type index) noexcept
    {
        return data_[index];
    }

    /**
     * @brief Provides unchecked const access to an element by index
     *
     * Const version of operator[]. Allows efficient read-only access to elements via array-like
     * syntax on const vectors. No bounds checking is performed. The caller is responsible for
     * ensuring the index is within [0, size()).
     *
     * @param index The zero-based index of the element to access.
     *              Must be less than size(); behavior is undefined otherwise.
     * @return A const reference to the element at the specified index.
     *
     * @complexity \f$ O(1) \f$
     * @exception noexcept
     *
     * @note For safety-critical code, use at(size_type) const instead, which performs bounds
     * checking.
     *
     * @see at(size_type) const for bounds-checked access
     */
    [[nodiscard]] constexpr const_reference operator[](size_type index) const noexcept
    {
        return data_[index];
    }

    /**
     * @brief Provides bounds-checked access to an element by index
     *
     * Safely accesses an element by index, throwing std::out_of_range if the index is invalid.
     * Slightly slower than operator[] due to bounds checking, but provides guaranteed safety.
     *
     * @param index The zero-based index of the element to access.
     *              Must be less than size().
     * @return A reference to the element at the specified index.
     *
     * @complexity \f$ O(1) \f$
     * @exception std::out_of_range if index >= size()
     *
     * @note Use this method when the index source is not under your control or when safety is
     *       more important than maximum performance.
     *
     * @see operator[](size_type) for unchecked access
     */
    [[nodiscard]] constexpr reference at(size_type index)
    {
        if (index >= size_) [[unlikely]]
            throw std::out_of_range("Index out of range");
        return data_[index];
    }

    /**
     * @brief Provides bounds-checked const access to an element by index
     *
     * Const version of at(). Safely accesses an element by index on const vectors,
     * throwing std::out_of_range if the index is invalid.
     *
     * @param index The zero-based index of the element to access.
     *              Must be less than size().
     * @return A const reference to the element at the specified index.
     *
     * @complexity \f$ O(1) \f$
     * @exception std::out_of_range if index >= size()
     *
     * @see operator[](size_type) const for unchecked access
     */
    [[nodiscard]] constexpr const_reference at(size_type index) const
    {
        if (index >= size_) [[unlikely]]
            throw std::out_of_range("Index out of range");
        return data_[index];
    }

    /**
     * @brief Unchecked access to the first element
     *
     * Returns a reference to the first element in the vector. Provides maximum efficiency for
     * accessing the front element. Behavior is undefined if the vector is empty.
     *
     * @return A reference to the first element of the vector.
     *
     * @complexity \f$ O(1) \f$
     * @exception noexcept
     *
     * @pre The vector must not be empty (size() > 0)
     *
     * @warning Calling this function on an empty vector results in undefined behavior.
     *          Use empty() to check before calling, or use begin() + bounds checking.
     *
     * @see back() for accessing the last element
     */
    [[nodiscard]] constexpr reference front() noexcept
    {
        return data_[0];
    }

    /**
     * @brief Unchecked const access to the first element
     *
     * Const version of front(). Returns a const reference to the first element.
     * Behavior is undefined if the vector is empty.
     *
     * @return A const reference to the first element of the vector.
     *
     * @complexity \f$ O(1) \f$
     * @exception noexcept
     *
     * @pre The vector must not be empty (size() > 0)
     *
     * @warning Calling this function on an empty vector results in undefined behavior.
     *
     * @see back() for accessing the last element
     */
    [[nodiscard]] constexpr const_reference front() const noexcept
    {
        return data_[0];
    }

    /**
     * @brief Unchecked access to the last element
     *
     * Returns a reference to the last element (at index size()-1). Provides maximum efficiency
     * for accessing the rear element. Behavior is undefined if the vector is empty.
     *
     * @return A reference to the last element of the vector.
     *
     * @complexity \f$ O(1) \f$
     * @exception noexcept
     *
     * @pre The vector must not be empty (size() > 0)
     *
     * @warning Calling this function on an empty vector results in undefined behavior.
     *          Use empty() to check before calling, or use rbegin() + bounds checking.
     *
     * @see front() for accessing the first element
     */
    [[nodiscard]] constexpr reference back() noexcept
    {
        return data_[size_ - 1];
    }

    /**
     * @brief Unchecked const access to the last element
     *
     * Const version of back(). Returns a const reference to the last element.
     * Behavior is undefined if the vector is empty.
     *
     * @return A const reference to the last element of the vector.
     *
     * @complexity \f$ O(1) \f$
     * @exception noexcept
     *
     * @pre The vector must not be empty (size() > 0)
     *
     * @warning Calling this function on an empty vector results in undefined behavior.
     *          Use empty() to check before calling, or use rbegin() + bounds checking
     *
     * @see front() for accessing the first element
     */
    [[nodiscard]] constexpr const_reference back() const noexcept
    {
        return data_[size_ - 1];
    }

    /**
     * @brief Provides direct pointer access to the underlying data array
     *
     * Returns a pointer to the first element of the underlying array. This allows direct pointer
     * manipulation and compatibility with C-style APIs expecting pointer arguments. Be careful not
     * to access elements beyond [data_, data_+size()).
     *
     * @return A pointer to the underlying element array, or nullptr if the vector is empty.
     *
     * @complexity \f$ O(1) \f$
     * @exception noexcept
     *
     * @note The returned pointer is valid only as long as no reallocation occurs. Reallocation can
     *       occur via push_back(), insert(), emplace_back(), resize(), reserve(), or similar.
     * @note For an empty vector, data() may return nullptr or a valid pointer to no actual
     * elements. Both behaviors are valid.
     *
     * @see get_view() for a safer view via std::span
     */
    [[nodiscard]] constexpr pointer data() noexcept
    {
        return data_;
    }

    /**
     * @brief Provides const pointer access to the underlying data array
     *
     * Const version of data(). Returns a const pointer to the first element, providing read-only
     * access to the underlying array. This allows C-style read-only pointer operations and use with
     * C APIs that accept const pointers.
     *
     * @return A const pointer to the underlying element array, or nullptr if the vector is empty.
     *
     * @complexity \f$ O(1) \f$
     * @exception noexcept
     *
     * @note The returned pointer is valid only as long as no reallocation occurs. Reallocation can
     *       occur via push_back(), insert(), emplace_back(), resize(), reserve(), or similar.
     * @note For an empty vector, data() may return nullptr or a valid pointer to no actual
     * elements. Both behaviors are valid.
     *
     * @see get_view() for a safer view via std::span
     */
    [[nodiscard]] constexpr const_pointer data() const noexcept
    {
        return data_;
    }

    /**
     * @brief Returns a std::span view of all elements in the vector
     *
     * Creates a non-owning std::span that views the vector's elements. This provides a safe
     * alternative to raw pointers, with bounds information included. The span is valid as long as
     * no reallocation occurs (i.e., no push_back(), insert(), reserve() that increases capacity).
     *
     * @return A std::span<T> viewing [data_, data_+size()).
     *
     * @complexity \f$ O(1) \f$
     * @exception noexcept
     *
     * @note This is a modern replacement for raw pointer access that maintains type and bounds
     *       information. Particularly useful for passing to functions expecting span parameters.
     * @note The span is invalidated if any operation causes reallocation. For empty vectors,
     *       the returned span is valid but empty.
     * @see std::span for documentation on the span view type
     */
    [[nodiscard]] constexpr std::span<T> get_view() noexcept
    {
        return std::span<T>(data_, size_);
    }

    /**
     * @brief Returns a const std::span view of all elements in the vector
     *
     * Creates a non-owning std::span that views the vector's elements. This provides a safe
     * alternative to raw pointers, with bounds information included. The span is valid as long as
     * no reallocation occurs (i.e., no push_back(), insert(), reserve() that increases capacity).
     *
     * @return A std::span<const T> viewing [data_, data_+size()).
     *
     * @complexity \f$ O(1) \f$
     * @exception noexcept
     *
     * @note This is a modern replacement for raw pointer access that maintains type and bounds
     *       information. Particularly useful for passing to functions expecting span parameters.
     * @note The span is invalidated if any operation causes reallocation. For empty vectors,
     *       the returned span is valid but empty.
     *
     * @see std::span for documentation on the span view type
     */
    [[nodiscard]] constexpr std::span<const T> get_view() const noexcept
    {
        return std::span<const T>(data_,
                                  size_); // Return a span that views the elements of the vector
    }

    /**
     * @brief Checks if the vector contains a specific value
     *
     * Searches the vector for an element equal to the provided value. Returns true if found,
     * false otherwise. Uses operator== for equality comparison.
     *
     * @param value The value to search for. Must be equality-comparable with T.
     * @return true if the vector contains an element equal to value; false otherwise.
     *
     * @complexity \f$ O(n) \f$
     * @exception noexcept
     *
     * @see find(const_reference) for retrieving an iterator to the found element
     */
    [[nodiscard]] constexpr bool contains(const_reference value) const noexcept
    {
        return std::ranges::find(get_view(), value) != get_view().end();
    }

    /**
     * @brief Finds the first occurrence of a specific value and returns an iterator
     *
     * Performs a linear search for the first element equal to the provided value.
     * Returns an iterator to the found element, or end() if not found.
     *
     * @param value The value to search for. Must be equality-comparable with T.
     * @return A const_iterator to the first matching element, or end() if not found.
     *
     * @complexity \f$ O(n) \f$
     * @exception noexcept
     *
     * @note For a boolean check, use contains() instead, which is semantically clearer.
     *
     * @see contains(const_reference) for just checking existence without an iterator
     */
    [[nodiscard]] constexpr const_iterator find(const_reference value) const noexcept
    {
        auto it = std::ranges::find(get_view(), value);
        if (it != get_view().end())
        {
            return iterator(
                data_ +
                (it - get_view().begin())); // Return an iterator pointing to the found element
        }
        else
        {
            return end(); // Return end() if the value is not found
        }
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
    constexpr allocator_type get_allocator() const noexcept
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
     * Returns the count of actual elements stored, not the allocated capacity.
     * This is always <= capacity().
     *
     * @return The current size (number of stored elements).
     *
     * @complexity \f$ O(1) \f$
     * @exception noexcept
     *
     * @note For checking emptiness, use empty() instead, which is more idiomatic.
     *
     * @see capacity() to get allocated storage
     * @see empty() for a boolean emptiness check
     */
    [[nodiscard]] constexpr size_type size() const noexcept
    {
        return size_;
    }

    /**
     * @brief Returns the theoretical maximum number of elements the vector can hold
     *
     * This represents the largest possible size the vector could reach, accounting for both
     * allocator limits and the maximum value representable by size_type. In practice, memory
     * availability is usually the limiting factor before reaching this value.
     *
     * @return The maximum possible size the vector can achieve.
     *
     * @complexity \f$ O(1) \f$
     * @exception noexcept
     *
     * @note This value is typically very large (billions of elements) and is not a practical limit
     *       in most applications. Actual allocation limits come from available memory.
     * @note Calculated as min(allocator::max_size(), std::numeric_limits<difference_type>::max())
     *       to ensure all pointer arithmetic remains valid.
     *
     * @see capacity() for the currently allocated size
     * @see size() for the number of elements currently stored
     */
    [[nodiscard]] constexpr size_type max_size() const noexcept
    {
        // Use difference_type as the distance between pointers must be representable by a signed
        // integer, not unsigned
        return std::min<size_type>(traits::max_size(alloc_),
                                   std::numeric_limits<difference_type>::max() /
                                       sizeof(value_type));
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
    [[nodiscard]] constexpr size_type capacity() const noexcept
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
    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return size_ == 0;
    }

    /**
     * @brief Returns the total bytes of memory allocated for the element storage
     *
     * Calculates and returns the number of bytes used for the internal buffer, equal to
     * capacity() * sizeof(T). Useful for memory profiling and debugging. Does not include
     * overhead from the vector structure itself or the allocator.
     *
     * @return Total bytes allocated for elements: capacity() * sizeof(T).
     *
     * @complexity \f$ O(1) \f$
     * @exception noexcept
     *
     * @note This is the raw allocated memory, not accounting for any allocator overhead,
     *       vector object overhead, or fragmentation.
     *
     * @see capacity() for the number of elements that can fit
     */
    [[nodiscard]] constexpr size_type memory_usage() const noexcept
    {
        return capacity_ * sizeof(value_type);
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
    constexpr void reserve(size_type new_capacity)
    {
        if (new_capacity > max_size()) [[unlikely]]
            throw std::length_error("vector::reserve: capacity exceeded max_size()");
        if (new_capacity > capacity_)
        {
            pointer new_data = traits::allocate(alloc_, new_capacity);

            size_type i = 0;
            try
            {
                for (; i < size_; ++i)
                {
                    traits::construct(alloc_, new_data + i, std::move_if_noexcept(data_[i]));
                }
            }
            catch (...)
            {
                // Clean up new buffer
                for (size_type j = 0; j < i; ++j)
                {
                    traits::destroy(alloc_, new_data + j);
                }
                traits::deallocate(alloc_, new_data, new_capacity);

                throw; // Rethrow the exception to propagate it to the caller
            }

            if (data_)
            {
                for (size_type j = 0; j < size_; ++j)
                {
                    traits::destroy(alloc_, data_ + j);
                }
                traits::deallocate(alloc_, data_, capacity_); // Deallocate old memory
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
    constexpr void shrink_to_fit()
    {
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
    constexpr void push_back(const_reference value)
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
    constexpr void push_back(T&& value)
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
    constexpr reference emplace_back(Args&&... args)
        requires std::constructible_from<T, Args...>
    {
        if (size_ >= capacity_)
            reallocate();

        traits::construct(alloc_, data_ + size_, std::forward<Args>(args)...);
        return data_[size_++];
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
    constexpr iterator insert(const_iterator pos, const_reference value)
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
    constexpr iterator insert(const_iterator pos, T&& value)
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
    constexpr iterator insert(const_iterator pos, size_type count, const_reference value)
        requires std::copy_constructible<T>
    {
        size_type offset = pos - cbegin();

        if (count == 0)
            return iterator(data_ + offset);

        if (size_ + count > max_size()) [[unlikely]]
            throw std::length_error("vector::insert: size exceeds max_size()");

        size_type index = pos - cbegin();

        if (size_ + count > capacity_)
        {
            // Reallocation needed

            // New capacity is either the current size plus count (to fit new elements) or 1.5x the
            // current capacity, whichever is larger
            size_type new_capacity = std::max(size_ + count, capacity_ + capacity_ / 2);
            vector temp(alloc_);
            temp.reserve(new_capacity);

            // Move elements before the insertion point
            for (size_type i = 0; i < index; ++i)
            {
                temp.push_back(std::move_if_noexcept(data_[i]));
            }

            // Insert the new elements
            for (size_type i = 0; i < count; ++i)
            {
                temp.emplace_back(value);
            }

            // Move elements after the insertion point
            for (size_type i = index; i < size_; ++i)
            {
                temp.push_back(std::move_if_noexcept(data_[i]));
            }

            this->swap(
                temp); // Swap with the temporary vector, which now has the new elements in place
            return iterator(
                data_ + index); // Return an iterator to the first of the newly inserted elements
        }
        else
        {
            // No reallocation needed, just shift elements in place
            size_type elements_moved = 0;
            try
            {
                // Move elements from the end to the right to make space for the new elements
                for (size_type i = size_; i > index; --i)
                {
                    traits::construct(alloc_, data_ + i + count - 1,
                                      std::move_if_noexcept(data_[i - 1]));
                    elements_moved++;
                    traits::destroy(alloc_, data_ + i - 1); // Destroy the old element after moving
                }

                // Insert the new elements in the vacated space
                size_type elements_inserted = 0;
                try
                {
                    for (; elements_inserted < count; ++elements_inserted)
                    {
                        traits::construct(alloc_, data_ + index + elements_inserted, value);
                    }
                }
                catch (...)
                {
                    // If construction of new elements fails, we need to roll back the inserted
                    // elements
                    for (size_type j = 0; j < elements_inserted; ++j)
                    {
                        traits::destroy(alloc_, data_ + index + j);
                    }

                    // Move the shifted elements back to their original positions
                    for (size_type j = 0; j < elements_moved; ++j)
                    {
                        traits::construct(alloc_, data_ + index + j,
                                          std::move_if_noexcept(data_[index + count + j]));
                        traits::destroy(alloc_, data_ + index + count + j);
                    }

                    elements_moved = 0;

                    throw; // Rethrow the exception to propagate it to the caller
                }
            }
            catch (...)
            {
                // If moving elements fails, we need to roll back any moved elements
                for (size_type j = 0; j < elements_moved; ++j)
                {
                    traits::construct(alloc_, data_ + index + j,
                                      std::move_if_noexcept(data_[index + count + j]));
                    traits::destroy(alloc_, data_ + index + count + j);
                }
                throw;
            }

            size_ += count; // Update size after successful insertion
            return iterator(
                data_ + index); // Return an iterator to the first of the newly inserted elements
        }
    }

    /**
     * @brief Inserts a range of elements at the specified position
     *
     * Inserts elements from the range [first, last) at the position indicated by `pos`. Elements at
     * and after `pos` are shifted to the right to make space for the new elements. If the vector is
     * at capacity, reallocation occurs automatically.
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
     * @complexity \f$ O(\mbox{size} + \mbox{distance(first, last)}) \f$
     *
     * @exception std::length_error if size() + distance(first, last) exceeds max_size()
     * @exception std::bad_alloc if reallocation needed but allocation fails
     * @exception ... Any exception thrown by T's copy/move constructor during shifting or
     * construction
     *
     * @exception_safety Strong exception safety: if insertion fails, the vector is unchanged.
     *
     * @note Invalidates iterators at and after `pos` and all pointers if reallocation occurs.
     */
    template <class InputIt>
    constexpr iterator insert(const_iterator pos, InputIt first, InputIt last)
        requires std::input_iterator<InputIt> &&
                 std::constructible_from<T, typename std::iterator_traits<InputIt>::reference>
    {
        size_type count = std::distance(first, last);
        if (count == 0)
            return iterator(data_ + (pos - cbegin()));

        if (size_ + count > max_size()) [[unlikely]]
            throw std::length_error("vector::insert: size exceeds max_size()");

        size_type index = pos - cbegin();

        if (size_ + count > capacity_)
        {
            // Reallocation needed

            // New capacity is either the current size plus count (to fit new elements) or 1.5x the
            // current capacity, whichever is larger
            size_type new_capacity = std::max(size_ + count, capacity_ + capacity_ / 2);
            vector temp(alloc_);
            temp.reserve(new_capacity);

            // Move elements before the insertion point
            for (size_type i = 0; i < index; ++i)
            {
                temp.push_back(std::move_if_noexcept(data_[i]));
            }

            // Insert the new elements
            for (InputIt it = first; it != last; ++it)
            {
                temp.emplace_back(*it);
            }

            // Move elements after the insertion point
            for (size_type i = index; i < size_; ++i)
            {
                temp.push_back(std::move_if_noexcept(data_[i]));
            }

            this->swap(
                temp); // Swap with the temporary vector, which now has the new elements in place
            return iterator(
                data_ + index); // Return an iterator to the first of the newly inserted elements
        }
        else
        {
            // No reallocation needed, just shift elements in place
            size_type elements_moved = 0;
            try
            {
                // Move elements from the end to the right to make space for the new elements
                for (size_type i = size_; i > index; --i)
                {
                    traits::construct(alloc_, data_ + i + count - 1,
                                      std::move_if_noexcept(data_[i - 1]));
                    elements_moved++;
                    traits::destroy(alloc_, data_ + i - 1); // Destroy the old element after moving
                }

                // Insert the new elements in the vacated space
                size_type elements_inserted = 0;
                try
                {
                    for (InputIt it = first; it != last; ++it, ++elements_inserted)
                    {
                        traits::construct(alloc_, data_ + index + elements_inserted, *it);
                    }
                }
                catch (...)
                {
                    // If construction of new elements fails, we need to roll back the inserted
                    // elements
                    for (size_type j = 0; j < elements_inserted; ++j)
                    {
                        traits::destroy(alloc_, data_ + index + j);
                    }
                    throw;
                }
            }
            catch (...)
            {
                // If shifting fails, we must restore the original elements that were
                // moved-to the right. For each moved element we previously constructed at
                // `data_ + index + count + j`, move it back into `data_ + index + j`
                // and destroy the temporary at the moved-to slot. This ensures the
                // original elements are reconstructed.
                for (size_type j = 0; j < elements_moved; ++j)
                {
                    traits::construct(alloc_, data_ + index + j,
                                      std::move_if_noexcept(data_[index + count + j]));
                    traits::destroy(alloc_, data_ + index + count + j);
                }
                throw;
            }

            size_ += count; // Update size after successful insertion
            return iterator(
                data_ + index); // Return an iterator to the first of the newly inserted elements
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
    constexpr iterator insert(const_iterator pos, std::initializer_list<T> ilist)
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
    template <class... Args> constexpr iterator emplace(const_iterator pos, Args&&... args)
    {
        size_type index = pos - cbegin();

        if (size_ + 1 > max_size()) [[unlikely]]
            throw std::length_error("vector::emplace: size exceeds max_size()");

        if (size_ >= capacity_)
        {
            // Reallocation needed
            size_type new_capacity = std::max(size_ + 1, capacity_ + capacity_ / 2);
            vector temp(alloc_);
            temp.reserve(new_capacity);

            // Move elements before the insertion point
            for (size_type i = 0; i < index; ++i)
            {
                temp.push_back(std::move_if_noexcept(data_[i]));
            }

            // Construct the new element in place
            temp.emplace_back(std::forward<Args>(args)...);

            // Move elements after the insertion point
            for (size_type i = index; i < size_; ++i)
            {
                temp.push_back(std::move_if_noexcept(data_[i]));
            }

            this->swap(
                temp); // Swap with the temporary vector, which now has the new element in place
        }
        else
        {
            // No reallocation needed, just shift elements in place and construct the new element
            size_type elements_moved = 0;
            try
            {
                // Move elements from the end to the right to make space for the new element
                for (size_type i = size_; i > index; --i)
                {
                    traits::construct(alloc_, data_ + i, std::move_if_noexcept(data_[i - 1]));
                    elements_moved++;
                    traits::destroy(alloc_, data_ + i - 1); // Destroy the old element after moving
                }

                // Construct the new element in place at the vacated position
                traits::construct(alloc_, data_ + index, std::forward<Args>(args)...);
            }
            catch (...)
            {
                // If construction of the new element fails, we need to roll back any moved elements
                for (size_type j = 0; j < elements_moved; ++j)
                {
                    traits::construct(alloc_, data_ + index + j,
                                      std::move_if_noexcept(data_[index + j]));
                    traits::destroy(alloc_, data_ + index + j);
                }
                throw; // Rethrow the exception to propagate it to the caller
            }

            ++size_; // Update size after successful insertion
        }

        return iterator(data_ + index); // Return an iterator to the newly constructed element
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
    constexpr void pop_back() noexcept
    {
        if (size_ > 0)
        {
            --size_;
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
    constexpr iterator
    erase(const_iterator pos) noexcept(std::is_nothrow_move_assignable_v<value_type>)
    {
        // Calculate raw index
        size_type index = pos - cbegin();

        // Shift elements after the removed element to the left
        for (size_type i = index + 1; i < size_; ++i)
        {
            data_[i - 1] = std::move_if_noexcept(data_[i]);
        }

        // Shrink the vector by one element and destroy the last element which is now a
        // duplicate/moved-from object after the shift
        --size_;
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
    constexpr iterator
    erase(const_iterator first,
          const_iterator last) noexcept(std::is_nothrow_move_assignable_v<value_type>)
    {
        size_type index_first = first - cbegin();

        if (first == last) [[unlikely]]
            return iterator(data_ + index_first); // No elements to erase, return iterator to first

        size_type index_last = last - cbegin();
        size_type count = index_last - index_first;

        // Shift elements after the removed range to the left
        for (size_type i = index_last; i < size_; ++i)
        {
            data_[i - count] = std::move_if_noexcept(data_[i]);
        }

        // Shrink the vector by the number of removed elements and destroy the now-unused elements
        // at the end
        size_ -= count;
        for (size_type i = size_; i < size_ + count; ++i)
        {
            traits::destroy(alloc_, data_ + i);
        }

        // Return an iterator pointing to the element that followed the removed range, or end() if
        // it was the last one
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
    constexpr void resize(size_type new_size)
    {
        if (new_size > max_size()) [[unlikely]]
            throw std::length_error("vector::resize: new size exceeds max_size()");

        if (new_size < size_)
        {
            // Shrink the vector by destroying elements from new size to old size
            while (size_ > new_size)
            {
                --size_;
                traits::destroy(alloc_, data_ + size_);
            }
        }
        else if (new_size > size_)
        {
            // Grow the vector, first ensure we have enough capacity for the new size
            if (new_size > capacity_)
            {
                reserve(new_size);
            }

            size_type original_size = size_;
            try
            {
                while (size_ < new_size)
                {
                    traits::construct(alloc_, data_ + size_);
                    ++size_; // Only increment after a successful construction
                }
            }
            catch (...)
            {
                // Destroy any newly constructed elements
                while (size_ > original_size)
                {
                    --size_;
                    traits::destroy(alloc_, data_ + size_);
                }

                throw; // Rethrow the exception to propagate it to the caller
            }
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
    constexpr void resize(size_type new_size, const_reference value)
    {
        if (new_size > max_size()) [[unlikely]]
            throw std::length_error("vector::resize: new size exceeds max_size()");

        if (new_size < size_)
        {
            // Shrink the vector by destroying elements from new size to old size
            while (size_ > new_size)
            {
                --size_;
                traits::destroy(alloc_, data_ + size_);
            }
        }
        else if (new_size > size_)
        {
            // Grow the vector, first ensure we have enough capacity for the new size
            if (new_size > capacity_)
            {
                reserve(new_size);
            }

            size_type original_size = size_;
            try
            {
                while (size_ < new_size)
                {
                    traits::construct(alloc_, data_ + size_, value);
                    ++size_; // Only increment after a successful construction
                }
            }
            catch (...)
            {
                // Destroy any newly constructed elements
                while (size_ > original_size)
                {
                    --size_;
                    traits::destroy(alloc_, data_ + size_);
                }

                throw; // Rethrow the exception to propagate it to the caller
            }
        }
    }

    /**
     * @brief Removes all elements from the vector, leaving it empty
     *
     * Destroys all contained elements by calling their destructors and sets size to zero.
     * After this operation, the vector is empty but has not released its allocated buffer.
     *
     * @complexity \f$ O(\mbox{size}) \f$
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
    constexpr void clear() noexcept
    {
        while (size_ > 0)
        {
            --size_;
            traits::destroy(alloc_, data_ + size_);
        }
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
    constexpr void assign(size_type count, const_reference value)
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
    constexpr void assign(InputIt first, InputIt last)
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
    constexpr void assign(std::initializer_list<T> init)
    {
        assign(init.begin(), init.end());
    }

    /**
     * @brief Exchanges the contents of this vector with another vector
     *
     * Efficiently swaps all internal state between two vectors: allocator, data pointer, size, and
     * capacity. After swap, the contents of this vector become those of `other`, and vice versa.
     *
     * @param other The other vector to swap with. Must be the same type and allocator.
     *
     * @complexity \f$ O(1) \f$
     * @exception noexcept(std::is_nothrow_swappable_v<Alloc>)
     *
     * @note Iterators remain valid but point to the other vector's elements (if iterators are
     *       saved, they now refer to elements that belonged to the other vector).
     * @note This is the underlying operation used by copy-and-swap idiom for exception safety.
     * @note Allocators are swapped (not copied), so allocator ownership is transferred.
     *
     * @see non-member swap(vector&, vector&) for the standard free function
     */
    constexpr void swap(vector& other) noexcept(std::is_nothrow_swappable_v<Alloc>)
    {
        using std::swap;
        swap(alloc_, other.alloc_);
        swap(data_, other.data_);
        swap(size_, other.size_);
        swap(capacity_, other.capacity_);
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
        constexpr reference operator*() const noexcept
        {
            return *ptr_;
        }

        /**
         * @brief Accesses a member of the pointed-to element via the arrow operator
         *
         * Equivalent to (*it).member, allowing direct member access syntax. Useful when iterating
         * over containers of complex objects.
         *
         * @return A pointer to the element, for member access.
         *
         * @complexity \f$ O(1) \f$
         * @exception noexcept
         *
         * @pre The iterator must be valid and dereferenceable (not past-the-end).
         * @pre The element must have accessible members (not a primitive type).
         *
         * @example
         * @code
         * for (auto it = vec.begin(); it != vec.end(); ++it)
         *     it->member = value;  // Uses operator->()
         * @endcode
         *
         * @see operator*() for dereferencing
         */
        constexpr pointer operator->() noexcept
        {
            return ptr_;
        }

        /**
         * @brief Accesses a member of the pointed-to element via the arrow operator (const version)
         *
         * Equivalent to (*it).member, allowing direct member access syntax. Useful when iterating
         * over containers of complex objects.
         *
         * @return A pointer to the element, for member access.
         *
         * @complexity \f$ O(1) \f$
         * @exception noexcept
         *
         * @pre The iterator must be valid and dereferenceable (not past-the-end).
         * @pre The element must have accessible members (not a primitive type).
         *
         * @example
         * @code
         * for (auto it = vec.begin(); it != vec.end(); ++it)
         *     it->member = value;  // Uses operator->()
         * @endcode
         *
         * @see operator*() for dereferencing
         */
        constexpr pointer operator->() const noexcept
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
        constexpr reference operator[](difference_type off) const noexcept
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
        constexpr base_iterator& operator++() noexcept
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
        constexpr base_iterator operator++(int) noexcept
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
        constexpr base_iterator& operator--() noexcept
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
        constexpr base_iterator operator--(int) noexcept
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
        constexpr base_iterator& operator+=(difference_type off) noexcept
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
        constexpr base_iterator& operator-=(difference_type off) noexcept
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
        constexpr friend base_iterator operator+(base_iterator it, difference_type off) noexcept
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
        constexpr friend base_iterator operator+(difference_type off, base_iterator it) noexcept
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
        constexpr friend base_iterator operator-(base_iterator it, difference_type off) noexcept
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
        constexpr friend difference_type operator-(const base_iterator& lhs,
                                                   const base_iterator& rhs) noexcept
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
     * Returns an iterator to the first element in the vector. If the vector is empty, the returned
     * iterator equals end(). Dereferencing an iterator to an empty vector results in undefined
     * behavior; check empty() first if unsure.
     *
     * @return An iterator to the first element (or end() if empty).
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
    [[nodiscard]] constexpr iterator begin() noexcept
    {
        return iterator(data_);
    }

    /**
     * @brief Returns a const iterator to the first element
     *
     * Returns a const iterator to the first element in the vector. Provides read-only iteration
     * starting from the beginning. If the vector is empty, returns an iterator equivalent to
     * cend().
     *
     * @return A const iterator to the first element (or cend() if empty).
     *
     * @complexity \f$ O(1) \f$
     * @exception noexcept
     *
     * @note Invalidated by any operation that causes reallocation (reserve, push_back, insert,
     * etc.).
     *
     * @see cend() for the corresponding past-the-end iterator
     */
    [[nodiscard]] constexpr const_iterator begin() const noexcept
    {
        return const_iterator(data_);
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
    [[nodiscard]] constexpr const_iterator cbegin() const noexcept
    {
        return const_iterator(data_);
    }

    /**
     * @brief Returns an iterator to one past the last element (the end sentinel)
     *
     * Returns an iterator pointing to the position one past the last valid element. This iterator
     * is used as a sentinel and should not be dereferenced. It is equal to begin() if the vector
     * is empty. Commonly used as the loop termination condition: for (auto it = v.begin(); it !=
     * v.end(); ++it)
     *
     * @return An iterator to one past the last element.
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
    [[nodiscard]] constexpr iterator end() noexcept
    {
        return iterator(data_ + size_);
    }

    /**
     * @brief Returns a const iterator to one past the last element
     *
     * Returns a const iterator to the end sentinel (one past the last element). Used for
     * read-only range iteration: for (auto it = v.begin(); it != v.end(); ++it)
     *
     * @return A const iterator to one past the last element.
     *
     * @complexity \f$ O(1) \f$
     * @exception noexcept
     *
     * @pre Do not dereference the returned iterator.
     *
     * @note Invalidated by any operation that causes reallocation (reserve, push_back, insert,
     * etc.).
     *
     * @see cbegin() for the corresponding first element iterator
     */
    [[nodiscard]] constexpr const_iterator end() const noexcept
    {
        return const_iterator(data_ + size_);
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
    [[nodiscard]] constexpr const_iterator cend() const noexcept
    {
        return const_iterator(data_ + size_);
    }

  private:
    // =========================================================
    // Helper functions
    // =========================================================

    /**
     * @brief Reallocates the internal buffer using 1.5x growth factor
     *
     * Internal helper function called when the vector needs more capacity. Calculates a new
     * capacity using a 1.5x growth strategy (capacity = 0 ? 1 : capacity * 1.5), then delegates
     * to reserve(new_capacity) to perform the actual reallocation and move.
     *
     * @complexity \f$ O(\mbox{size}) \f$
     *
     * @exception std::length_error if the new capacity exceeds max_size()
     * @exception std::bad_alloc if allocation fails
     * @exception ... Any exception thrown by T's move/copy constructor
     *
     * @exception_safety Strong: on exception, vector is unchanged
     *
     * @note This function is called automatically by push_back(), emplace_back(), and similar
     *       operations when capacity is exceeded.
     * @note The 1.5x strategy balances between avoiding frequent reallocations and wasting memory.
     * @note Invalidates all iterators and pointers.
     *
     * @see reserve(size_type) for requesting specific capacity
     */
    constexpr void reallocate()
    {
        // Calculate new capacity: 1.5x growth or start at 1
        // We use size_type to avoid floating point math where possible
        size_type new_capacity = (capacity_ == 0) ? 1 : capacity_ + capacity_ / 2;

        // 2x growth factor for testing
        // size_type new_capacity = (capacity_ == 0) ? 1 : capacity_ * 2;

        // Ensure that if capacity was 1, it actually grows (1 + 1/2 = 1 in integer math)
        if (new_capacity <= capacity_)
        {
            new_capacity = capacity_ + 1;
        }

        reserve(new_capacity);
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
constexpr void swap(vector<T, Alloc>& lhs, vector<T, Alloc>& rhs) noexcept
{
    lhs.swap(rhs);
}

} // namespace forge