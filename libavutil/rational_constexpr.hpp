/*
 * Modern C++ constexpr rational number utilities
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
 * Modern C++20 constexpr rational number utilities
 *
 * This file demonstrates modern C++ features that can benefit FFmpeg:
 * - constexpr functions for compile-time computation
 * - Strong type safety with enum class
 * - Zero-overhead abstractions
 *
 * These functions can be computed at compile time when arguments are
 * compile-time constants, providing performance benefits without runtime cost.
 */

#ifndef AVUTIL_RATIONAL_CONSTEXPR_HPP
#define AVUTIL_RATIONAL_CONSTEXPR_HPP

#include <cstdint>
#include <algorithm>

extern "C" {
#include "rational.h"
}

namespace ffmpeg {

/**
 * Compile-time GCD using binary GCD algorithm (Stein's algorithm)
 * This can be computed at compile time when inputs are constexpr
 */
constexpr int64_t gcd(int64_t a, int64_t b) noexcept {
    if (a == 0) return b;
    if (b == 0) return a;

    a = a < 0 ? -a : a;
    b = b < 0 ? -b : b;

    // Count trailing zeros
    int za = 0, zb = 0;
    while (((a >> za) & 1) == 0) ++za;
    while (((b >> zb) & 1) == 0) ++zb;

    int k = std::min(za, zb);
    int64_t u = a >> za;
    int64_t v = b >> zb;

    while (u != v) {
        if (u > v) {
            int64_t temp = u;
            u = v;
            v = temp;
        }
        v -= u;
        while ((v & 1) == 0) v >>= 1;
    }

    return u << k;
}

/**
 * Compile-time rational number multiplication
 * Result is automatically reduced to lowest terms
 */
constexpr AVRational mul_q(AVRational a, AVRational b) noexcept {
    int64_t num = static_cast<int64_t>(a.num) * b.num;
    int64_t den = static_cast<int64_t>(a.den) * b.den;

    // Reduce to lowest terms
    int64_t g = gcd(num, den);
    if (g != 0) {
        num /= g;
        den /= g;
    }

    // Clamp to INT_MAX
    while (num > INT_MAX || den > INT_MAX || num < INT_MIN || den < INT_MIN) {
        num >>= 1;
        den >>= 1;
    }

    return AVRational{static_cast<int>(num), static_cast<int>(den)};
}

/**
 * Compile-time rational number division
 */
constexpr AVRational div_q(AVRational a, AVRational b) noexcept {
    return mul_q(a, AVRational{b.den, b.num});
}

/**
 * Compile-time rational number addition
 */
constexpr AVRational add_q(AVRational a, AVRational b) noexcept {
    int64_t num = static_cast<int64_t>(a.num) * b.den +
                  static_cast<int64_t>(b.num) * a.den;
    int64_t den = static_cast<int64_t>(a.den) * b.den;

    int64_t g = gcd(num, den);
    if (g != 0) {
        num /= g;
        den /= g;
    }

    while (num > INT_MAX || den > INT_MAX || num < INT_MIN || den < INT_MIN) {
        num >>= 1;
        den >>= 1;
    }

    return AVRational{static_cast<int>(num), static_cast<int>(den)};
}

/**
 * Compile-time rational number subtraction
 */
constexpr AVRational sub_q(AVRational a, AVRational b) noexcept {
    return add_q(a, AVRational{-b.num, b.den});
}

/**
 * Compile-time rational number comparison
 * Returns: -1 if a < b, 0 if a == b, 1 if a > b
 */
constexpr int cmp_q(AVRational a, AVRational b) noexcept {
    int64_t tmp = static_cast<int64_t>(a.num) * b.den -
                  static_cast<int64_t>(b.num) * a.den;
    return (tmp > 0) - (tmp < 0);
}

/**
 * Type-safe rounding mode enum
 * Replaces preprocessor macros with proper types
 */
enum class RoundMode : int {
    Zero         = 0,
    Inf          = 1,
    Down         = 2,
    Up           = 3,
    NearInf      = 5,
    PassMinMax   = 8192,
};

/**
 * Compile-time minimum
 */
template<typename T>
constexpr T min(T a, T b) noexcept {
    return a < b ? a : b;
}

/**
 * Compile-time maximum
 */
template<typename T>
constexpr T max(T a, T b) noexcept {
    return a > b ? a : b;
}

/**
 * Compile-time absolute value
 */
template<typename T>
constexpr T abs(T val) noexcept {
    return val < 0 ? -val : val;
}

// Example: Common frame rates as compile-time constants
namespace FrameRates {
    constexpr AVRational fps_24    = {24, 1};
    constexpr AVRational fps_25    = {25, 1};
    constexpr AVRational fps_30    = {30, 1};
    constexpr AVRational fps_60    = {60, 1};
    constexpr AVRational fps_23_976 = {24000, 1001};
    constexpr AVRational fps_29_97  = {30000, 1001};
    constexpr AVRational fps_59_94  = {60000, 1001};

    // Compile-time check if a rate is a common frame rate
    constexpr bool is_common_frame_rate(AVRational rate) noexcept {
        return cmp_q(rate, fps_24) == 0 ||
               cmp_q(rate, fps_25) == 0 ||
               cmp_q(rate, fps_30) == 0 ||
               cmp_q(rate, fps_60) == 0 ||
               cmp_q(rate, fps_23_976) == 0 ||
               cmp_q(rate, fps_29_97) == 0 ||
               cmp_q(rate, fps_59_94) == 0;
    }
}

// Example: Compile-time computation of frame duration
// If frame rate is known at compile time, duration is computed at compile time
constexpr AVRational frame_duration(AVRational fps) noexcept {
    return AVRational{fps.den, fps.num};
}

// Static assertions for compile-time validation
static_assert(gcd(48, 18) == 6, "GCD computation failed");
static_assert(cmp_q(AVRational{1, 2}, AVRational{1, 3}) > 0, "Rational comparison failed");

} // namespace ffmpeg

#endif // AVUTIL_RATIONAL_CONSTEXPR_HPP
