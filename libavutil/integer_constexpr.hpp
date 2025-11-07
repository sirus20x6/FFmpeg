/*
 * Modern C++ arbitrary precision integers with constexpr
 * Copyright (c) 2004 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2025 FFmpeg Modernization Project
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * Modern C++20 arbitrary precision integers with constexpr support
 *
 * This header provides compile-time capable arbitrary precision integer
 * operations. All functions can be evaluated at compile time when arguments
 * are constexpr, enabling zero-cost abstractions.
 *
 * Features:
 * - Constexpr arithmetic operations
 * - Operator overloading for natural syntax
 * - Type-safe comparisons
 * - Compile-time evaluation support
 */

#ifndef AVUTIL_INTEGER_CONSTEXPR_HPP
#define AVUTIL_INTEGER_CONSTEXPR_HPP

#include <cstdint>
#include <cstring>
#include <array>

extern "C" {
#include "integer.h"
#include "intmath.h"
}

namespace ffmpeg {
namespace integer {

/**
 * Constexpr wrapper for AVInteger providing operator overloading
 * and compile-time operations
 */
class Integer {
private:
    AVInteger value_;

    // Helper for log2 computation
    static constexpr int log2_16bit_constexpr(uint16_t v) noexcept {
        if (v == 0) return -1;
        int n = 0;
        if (v & 0xff00) {
            v >>= 8;
            n += 8;
        }
        // Simplified log2 lookup
        while (v > 1) {
            v >>= 1;
            n++;
        }
        return n;
    }

public:
    static constexpr int SIZE = AV_INTEGER_SIZE;

    // Constructors
    constexpr Integer() noexcept : value_{} {}

    constexpr explicit Integer(const AVInteger& av) noexcept : value_(av) {}

    constexpr explicit Integer(int64_t val) noexcept : value_{} {
        for (int i = 0; i < SIZE; i++) {
            value_.v[i] = static_cast<uint16_t>(val);
            val >>= 16;
        }
    }

    // Access
    constexpr const AVInteger& av_integer() const noexcept {
        return value_;
    }

    constexpr uint16_t& operator[](int i) noexcept {
        return value_.v[i];
    }

    constexpr const uint16_t& operator[](int i) const noexcept {
        return value_.v[i];
    }

    // Addition
    constexpr Integer operator+(const Integer& other) const noexcept {
        Integer result = *this;
        int carry = 0;
        for (int i = 0; i < SIZE; i++) {
            carry = (carry >> 16) + result[i] + other[i];
            result[i] = static_cast<uint16_t>(carry);
        }
        return result;
    }

    constexpr Integer& operator+=(const Integer& other) noexcept {
        *this = *this + other;
        return *this;
    }

    // Subtraction
    constexpr Integer operator-(const Integer& other) const noexcept {
        Integer result = *this;
        int carry = 0;
        for (int i = 0; i < SIZE; i++) {
            carry = (carry >> 16) + result[i] - other[i];
            result[i] = static_cast<uint16_t>(carry);
        }
        return result;
    }

    constexpr Integer& operator-=(const Integer& other) noexcept {
        *this = *this - other;
        return *this;
    }

    // Unary minus
    constexpr Integer operator-() const noexcept {
        return Integer(0) - *this;
    }

    // Log2 (index of highest set bit)
    constexpr int log2() const noexcept {
        for (int i = SIZE - 1; i >= 0; i--) {
            if (value_.v[i]) {
                return log2_16bit_constexpr(value_.v[i]) + 16 * i;
            }
        }
        return -1;
    }

    // Multiplication
    constexpr Integer operator*(const Integer& other) const noexcept {
        Integer out;
        int na = (log2() + 16) >> 4;
        int nb = (other.log2() + 16) >> 4;

        for (int i = 0; i < na; i++) {
            unsigned int carry = 0;
            if (value_.v[i]) {
                for (int j = i; j < SIZE && j - i <= nb; j++) {
                    carry = (carry >> 16) + out[j] +
                            value_.v[i] * static_cast<unsigned>(other[j - i]);
                    out[j] = static_cast<uint16_t>(carry);
                }
            }
        }
        return out;
    }

    constexpr Integer& operator*=(const Integer& other) noexcept {
        *this = *this * other;
        return *this;
    }

    // Comparison operators
    constexpr int compare(const Integer& other) const noexcept {
        int v = static_cast<int16_t>(value_.v[SIZE - 1]) -
                static_cast<int16_t>(other[SIZE - 1]);
        if (v) return (v >> 16) | 1;

        for (int i = SIZE - 2; i >= 0; i--) {
            int v = value_.v[i] - other[i];
            if (v) return (v >> 16) | 1;
        }
        return 0;
    }

    constexpr bool operator==(const Integer& other) const noexcept {
        return compare(other) == 0;
    }

    constexpr bool operator!=(const Integer& other) const noexcept {
        return compare(other) != 0;
    }

    constexpr bool operator<(const Integer& other) const noexcept {
        return compare(other) < 0;
    }

    constexpr bool operator<=(const Integer& other) const noexcept {
        return compare(other) <= 0;
    }

    constexpr bool operator>(const Integer& other) const noexcept {
        return compare(other) > 0;
    }

    constexpr bool operator>=(const Integer& other) const noexcept {
        return compare(other) >= 0;
    }

    // Bit shift right (positive s) or left (negative s)
    constexpr Integer operator>>(int s) const noexcept {
        Integer out;
        for (int i = 0; i < SIZE; i++) {
            unsigned int index = i + (s >> 4);
            unsigned int v = 0;
            if (index + 1 < SIZE) v = value_.v[index + 1] * (1U << 16);
            if (index < SIZE) v |= value_.v[index];
            out[i] = static_cast<uint16_t>(v >> (s & 15));
        }
        return out;
    }

    constexpr Integer operator<<(int s) const noexcept {
        return *this >> (-s);
    }

    constexpr Integer& operator>>=(int s) noexcept {
        *this = *this >> s;
        return *this;
    }

    constexpr Integer& operator<<=(int s) noexcept {
        *this = *this << s;
        return *this;
    }

    // Convert to int64_t
    constexpr int64_t to_int64() const noexcept {
        uint64_t out = value_.v[3];
        for (int i = 2; i >= 0; i--) {
            out = (out << 16) | value_.v[i];
        }
        return static_cast<int64_t>(out);
    }

    // Check if zero
    constexpr bool is_zero() const noexcept {
        for (int i = 0; i < SIZE; i++) {
            if (value_.v[i] != 0) return false;
        }
        return true;
    }

    // Check if negative
    constexpr bool is_negative() const noexcept {
        return static_cast<int16_t>(value_.v[SIZE - 1]) < 0;
    }
};

// Compile-time constants
constexpr Integer zero{0};
constexpr Integer one{1};
constexpr Integer negative_one{-1};

// Division and modulo (non-constexpr due to mutable state requirements)
// These are implemented in the .cpp file
Integer divide(const Integer& a, const Integer& b, Integer* remainder = nullptr) noexcept;

inline Integer operator/(const Integer& a, const Integer& b) noexcept {
    return divide(a, b, nullptr);
}

inline Integer operator%(const Integer& a, const Integer& b) noexcept {
    Integer remainder;
    divide(a, b, &remainder);
    return remainder;
}

// Compile-time tests
namespace tests {
    // Basic arithmetic
    constexpr Integer test_add = Integer(100) + Integer(200);
    static_assert(test_add.to_int64() == 300, "Addition test failed");

    constexpr Integer test_sub = Integer(500) - Integer(200);
    static_assert(test_sub.to_int64() == 300, "Subtraction test failed");

    constexpr Integer test_mul = Integer(10) * Integer(20);
    static_assert(test_mul.to_int64() == 200, "Multiplication test failed");

    // Comparisons
    constexpr Integer a{100};
    constexpr Integer b{200};
    static_assert(a < b, "Comparison test failed");
    static_assert(!(a > b), "Comparison test failed");
    static_assert(a == Integer(100), "Equality test failed");
    static_assert(a != b, "Inequality test failed");

    // Bit operations
    constexpr Integer test_shift = Integer(16) >> 1;
    static_assert(test_shift.to_int64() == 8, "Right shift test failed");

    constexpr Integer test_left_shift = Integer(8) << 1;
    static_assert(test_left_shift.to_int64() == 16, "Left shift test failed");

    // Log2
    constexpr Integer test_log2_val{256};
    constexpr int log2_result = test_log2_val.log2();
    static_assert(log2_result == 8, "Log2 test failed");

    // Zero tests
    constexpr Integer test_zero{0};
    static_assert(test_zero.is_zero(), "Zero test failed");
    static_assert(zero.is_zero(), "Zero constant test failed");
}

} // namespace integer
} // namespace ffmpeg

#endif // AVUTIL_INTEGER_CONSTEXPR_HPP
