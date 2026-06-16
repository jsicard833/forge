#pragma once

#include <algorithm>
#include <array>
#include <utility>

struct BigObject
{
    BigObject() = default;
    explicit BigObject(int val) : data{}
    {
        std::fill(data.begin(), data.end(), val);
    }

    BigObject(const BigObject& other) : data(other.data) {}
    BigObject(BigObject&& other) noexcept : data(std::move(other.data)) {}

    BigObject& operator=(const BigObject& other)
    {
        if (this != &other)
        {
            data = other.data;
        }
        return *this;
    }
    BigObject& operator=(BigObject&& other) noexcept
    {
        if (this != &other)
        {
            data = std::move(other.data);
        }
        return *this;
    }

    ~BigObject() {}

    std::array<int, 256> data;
};