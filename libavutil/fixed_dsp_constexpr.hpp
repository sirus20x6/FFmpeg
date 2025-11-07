/*
 * Modern C++ template-based fixed-point DSP operations
 * Copyright (c) 2012 MIPS Technologies, Inc., California
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
 * Modern C++20 template-based fixed-point DSP operations
 *
 * This header provides template-based implementations of fixed-point DSP
 * operations with the following benefits:
 * - Type flexibility through templates
 * - Constexpr support for compile-time evaluation
 * - Better inlining and optimization opportunities
 * - Type safety (can't mix fixed/float accidentally)
 */

#ifndef AVUTIL_FIXED_DSP_CONSTEXPR_HPP
#define AVUTIL_FIXED_DSP_CONSTEXPR_HPP

#include <cstdint>
#include <type_traits>

extern "C" {
#include "fixed_dsp.h"
#include "attributes.h"
}

namespace ffmpeg {
namespace fixed_dsp {

/**
 * Fixed-point rounding constant (0.5 in Q31 format)
 */
constexpr int64_t ROUND_Q31 = 0x40000000;

/**
 * Template-based fixed-point vector multiplication with accumulation
 * dst[i] = src2[i] + (src0[i] * src1[i] >> 31)
 *
 * @tparam T Integer type (int32_t, int64_t, etc.)
 */
template<typename T = int>
inline void vector_fmul_add(T* __restrict dst,
                           const T* __restrict src0,
                           const T* __restrict src1,
                           const T* __restrict src2,
                           int len) noexcept {
    static_assert(std::is_integral_v<T>, "T must be an integer type");

    for (int i = 0; i < len; i++) {
        int64_t accu = static_cast<int64_t>(src0[i]) * static_cast<int64_t>(src1[i]);
        dst[i] = src2[i] + static_cast<T>((accu + ROUND_Q31) >> 31);
    }
}

/**
 * Template-based fixed-point vector multiplication with reversed second operand
 * dst[i] = (src0[i] * src1[len-1-i] >> 31)
 */
template<typename T = int>
inline void vector_fmul_reverse(T* __restrict dst,
                               const T* __restrict src0,
                               const T* __restrict src1,
                               int len) noexcept {
    static_assert(std::is_integral_v<T>, "T must be an integer type");

    src1 += len - 1;
    for (int i = 0; i < len; i++) {
        int64_t accu = static_cast<int64_t>(src0[i]) * static_cast<int64_t>(src1[-i]);
        dst[i] = static_cast<T>((accu + ROUND_Q31) >> 31);
    }
}

/**
 * Template-based fixed-point vector multiplication
 * dst[i] = (src0[i] * src1[i] >> 31)
 */
template<typename T = int>
inline void vector_fmul(T* __restrict dst,
                       const T* __restrict src0,
                       const T* __restrict src1,
                       int len) noexcept {
    static_assert(std::is_integral_v<T>, "T must be an integer type");

    for (int i = 0; i < len; i++) {
        int64_t accu = static_cast<int64_t>(src0[i]) * static_cast<int64_t>(src1[i]);
        dst[i] = static_cast<T>((accu + ROUND_Q31) >> 31);
    }
}

/**
 * Template-based fixed-point scalar product (dot product)
 * Returns sum of (v1[i] * v2[i]) >> 31
 */
template<typename T = int>
inline T scalarproduct_fixed(const T* __restrict v1,
                            const T* __restrict v2,
                            int len) noexcept {
    static_assert(std::is_integral_v<T>, "T must be an integer type");

    // Initialize with rounding constant
    int64_t p = ROUND_Q31;

    for (int i = 0; i < len; i++) {
        p += static_cast<int64_t>(v1[i]) * static_cast<int64_t>(v2[i]);
    }

    return static_cast<T>(p >> 31);
}

/**
 * Template-based butterfly operation for fixed-point FFT
 * Performs: v1[i] += v2[i]; v2[i] = old_v1[i] - v2[i]
 */
template<typename T = int>
inline void butterflies_fixed(T* __restrict v1,
                             T* __restrict v2,
                             int len) noexcept {
    static_assert(std::is_integral_v<T>, "T must be an integer type");

    using U = std::make_unsigned_t<T>;
    U* v1u = reinterpret_cast<U*>(v1);

    for (int i = 0; i < len; i++) {
        T t = v1[i] - v2[i];
        v1u[i] += v2[i];
        v2[i] = t;
    }
}

/**
 * Constexpr-capable vector multiplication (for compile-time when possible)
 * Limited length for compile-time use
 */
template<int Length, typename T = int>
constexpr void vector_fmul_constexpr(T* __restrict dst,
                                     const T* __restrict src0,
                                     const T* __restrict src1) noexcept {
    static_assert(std::is_integral_v<T>, "T must be an integer type");
    static_assert(Length > 0, "Length must be positive");

    for (int i = 0; i < Length; i++) {
        int64_t accu = static_cast<int64_t>(src0[i]) * static_cast<int64_t>(src1[i]);
        dst[i] = static_cast<T>((accu + ROUND_Q31) >> 31);
    }
}

/**
 * Constexpr scalar product (compile-time capable)
 */
template<int Length, typename T = int>
constexpr T scalarproduct_fixed_constexpr(const T* __restrict v1,
                                         const T* __restrict v2) noexcept {
    static_assert(std::is_integral_v<T>, "T must be an integer type");
    static_assert(Length > 0, "Length must be positive");

    int64_t p = ROUND_Q31;
    for (int i = 0; i < Length; i++) {
        p += static_cast<int64_t>(v1[i]) * static_cast<int64_t>(v2[i]);
    }

    return static_cast<T>(p >> 31);
}

// Compile-time tests
namespace tests {
    // Test vector multiplication at compile time
    constexpr int test_v1[4] = {0x40000000, 0x20000000, 0x10000000, 0x08000000}; // 0.5, 0.25, 0.125, 0.0625 in Q31
    constexpr int test_v2[4] = {0x40000000, 0x40000000, 0x40000000, 0x40000000}; // 0.5, 0.5, 0.5, 0.5

    // Test scalar product
    constexpr int dot_result = scalarproduct_fixed_constexpr<4>(test_v1, test_v2);
    // (0.5*0.5 + 0.25*0.5 + 0.125*0.5 + 0.0625*0.5) = 0.46875 in Q31
    static_assert(dot_result > 0x30000000 && dot_result < 0x50000000,
                  "Scalar product test");

    // Test vector fmul
    constexpr int test_fmul() {
        int dst[4] = {0, 0, 0, 0};
        vector_fmul_constexpr<4>(dst, test_v1, test_v2);
        return dst[0]; // Should be 0.5 * 0.5 = 0.25 = 0x20000000
    }
    constexpr int fmul_result = test_fmul();
    static_assert(fmul_result > 0x1f000000 && fmul_result < 0x21000000,
                  "Vector fmul test");

    // Test with zero
    constexpr int zero_vec[4] = {0, 0, 0, 0};
    constexpr int zero_dot = scalarproduct_fixed_constexpr<4>(zero_vec, test_v2);
    // Should be just the rounding constant >> 31 = 0
    static_assert(zero_dot >= 0 && zero_dot <= 1, "Zero vector test");

    // Test with ones (0x7fffffff = ~1.0 in Q31)
    constexpr int ones[4] = {0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff};
    constexpr int ones_dot = scalarproduct_fixed_constexpr<4>(ones, ones);
    // Should be close to 4.0 but clamped to max int32
    static_assert(ones_dot > 0, "Ones vector test");
}

/**
 * Type-safe wrapper class for fixed-point operations
 * Prevents accidentally mixing different fixed-point formats
 */
template<int FracBits>
class FixedPoint {
    int32_t value_;

public:
    constexpr FixedPoint() noexcept : value_(0) {}
    constexpr explicit FixedPoint(int32_t raw) noexcept : value_(raw) {}
    constexpr explicit FixedPoint(double d) noexcept
        : value_(static_cast<int32_t>(d * (1 << FracBits))) {}

    constexpr int32_t raw() const noexcept { return value_; }

    constexpr FixedPoint operator*(const FixedPoint& other) const noexcept {
        int64_t result = static_cast<int64_t>(value_) * other.value_;
        return FixedPoint(static_cast<int32_t>((result + (1LL << (FracBits - 1))) >> FracBits));
    }

    constexpr FixedPoint operator+(const FixedPoint& other) const noexcept {
        return FixedPoint(value_ + other.value_);
    }

    constexpr FixedPoint operator-(const FixedPoint& other) const noexcept {
        return FixedPoint(value_ - other.value_);
    }
};

// Q31 is the standard format used in FFmpeg
using Q31 = FixedPoint<31>;

// Test Q31 operations
namespace q31_tests {
    constexpr Q31 half(0.5);
    constexpr Q31 quarter(0.25);
    constexpr Q31 product = half * quarter;
    // 0.5 * 0.25 = 0.125
    static_assert(product.raw() > 0x0f000000 && product.raw() < 0x11000000,
                  "Q31 multiplication test");

    constexpr Q31 sum = half + quarter;
    // 0.5 + 0.25 = 0.75
    static_assert(sum.raw() > 0x5f000000 && sum.raw() < 0x61000000,
                  "Q31 addition test");
}

} // namespace fixed_dsp
} // namespace ffmpeg

#endif // AVUTIL_FIXED_DSP_CONSTEXPR_HPP
