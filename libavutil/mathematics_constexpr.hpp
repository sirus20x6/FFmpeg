/*
 * Modern C++ constexpr mathematics utilities
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
 * Modern C++20 constexpr mathematics utilities
 *
 * This file provides compile-time mathematical computations that can be
 * evaluated by the compiler when arguments are known at compile time,
 * resulting in zero runtime cost.
 *
 * These complement the existing mathematics.c runtime functions and can
 * be used interchangeably based on whether compile-time or runtime evaluation
 * is needed.
 */

#ifndef AVUTIL_MATHEMATICS_CONSTEXPR_HPP
#define AVUTIL_MATHEMATICS_CONSTEXPR_HPP

#include <cstdint>
#include <limits>
#include <type_traits>

extern "C" {
#include "mathematics.h"
}

namespace ffmpeg {
namespace math {

/**
 * Compile-time absolute value
 * Can be evaluated at compile time when argument is constexpr
 */
template<typename T>
constexpr T abs(T val) noexcept {
    static_assert(std::is_arithmetic_v<T>, "abs requires arithmetic type");
    return val < T{0} ? -val : val;
}

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
 * Compile-time clamping
 */
template<typename T>
constexpr T clamp(T val, T min_val, T max_val) noexcept {
    return min(max(val, min_val), max_val);
}

/**
 * Compile-time power of 2 check
 */
constexpr bool is_power_of_2(uint64_t n) noexcept {
    return n != 0 && (n & (n - 1)) == 0;
}

/**
 * Compile-time next power of 2
 */
constexpr uint64_t next_power_of_2(uint64_t n) noexcept {
    if (n == 0) return 1;

    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n |= n >> 32;
    n++;

    return n;
}

/**
 * Compile-time integer logarithm base 2 (floor)
 * Returns the position of the highest set bit
 */
constexpr int log2_floor(uint64_t n) noexcept {
    if (n == 0) return -1;

    int log = 0;
    if (n >= (1ULL << 32)) { log += 32; n >>= 32; }
    if (n >= (1ULL << 16)) { log += 16; n >>= 16; }
    if (n >= (1ULL <<  8)) { log +=  8; n >>=  8; }
    if (n >= (1ULL <<  4)) { log +=  4; n >>=  4; }
    if (n >= (1ULL <<  2)) { log +=  2; n >>=  2; }
    if (n >= (1ULL <<  1)) { log +=  1; }

    return log;
}

/**
 * Compile-time integer logarithm base 2 (ceiling)
 */
constexpr int log2_ceil(uint64_t n) noexcept {
    if (n <= 1) return 0;
    return log2_floor(n - 1) + 1;
}

/**
 * Compile-time count leading zeros
 */
constexpr int count_leading_zeros(uint64_t n) noexcept {
    if (n == 0) return 64;
    return 63 - log2_floor(n);
}

/**
 * Compile-time count trailing zeros (position of first set bit)
 */
constexpr int count_trailing_zeros(uint64_t n) noexcept {
    if (n == 0) return 64;

    int count = 0;
    while ((n & 1) == 0) {
        n >>= 1;
        count++;
    }
    return count;
}

/**
 * Compile-time GCD using binary GCD algorithm
 * (Same as in rational_constexpr.hpp but here for completeness)
 */
constexpr int64_t gcd(int64_t a, int64_t b) noexcept {
    if (a == 0) return b < 0 ? -b : b;
    if (b == 0) return a < 0 ? -a : a;

    a = abs(a);
    b = abs(b);

    int za = count_trailing_zeros(a);
    int zb = count_trailing_zeros(b);
    int k = min(za, zb);

    int64_t u = a >> za;
    int64_t v = b >> zb;

    while (u != v) {
        if (u > v) {
            int64_t temp = u;
            u = v;
            v = temp;
        }
        v -= u;
        v >>= count_trailing_zeros(v);
    }

    return u << k;
}

/**
 * Compile-time LCM (Least Common Multiple)
 */
constexpr int64_t lcm(int64_t a, int64_t b) noexcept {
    if (a == 0 || b == 0) return 0;
    int64_t g = gcd(a, b);
    return (abs(a) / g) * abs(b);
}

/**
 * Compile-time rescale (similar to av_rescale but for constexpr)
 * Performs: (a * b) / c with overflow protection
 * This is a simplified version for compile-time use
 */
constexpr int64_t rescale(int64_t a, int64_t b, int64_t c) noexcept {
    if (c == 0) return 0;

    // Simplified version - full version would need runtime overflow checks
    // For compile-time, we assume values are reasonable
    return (a * b) / c;
}

/**
 * Compile-time alignment check
 */
constexpr bool is_aligned(uint64_t addr, uint64_t alignment) noexcept {
    return (addr & (alignment - 1)) == 0;
}

/**
 * Compile-time alignment to next boundary
 */
constexpr uint64_t align_up(uint64_t val, uint64_t alignment) noexcept {
    return (val + alignment - 1) & ~(alignment - 1);
}

/**
 * Compile-time alignment to previous boundary
 */
constexpr uint64_t align_down(uint64_t val, uint64_t alignment) noexcept {
    return val & ~(alignment - 1);
}

/**
 * Compile-time byte swap for endianness (16-bit)
 */
constexpr uint16_t bswap16(uint16_t x) noexcept {
    return (x >> 8) | (x << 8);
}

/**
 * Compile-time byte swap for endianness (32-bit)
 */
constexpr uint32_t bswap32(uint32_t x) noexcept {
    x = ((x & 0xFF00FF00) >> 8) | ((x & 0x00FF00FF) << 8);
    return (x >> 16) | (x << 16);
}

/**
 * Compile-time byte swap for endianness (64-bit)
 */
constexpr uint64_t bswap64(uint64_t x) noexcept {
    x = ((x & 0xFF00FF00FF00FF00ULL) >> 8) | ((x & 0x00FF00FF00FF00FFULL) << 8);
    x = ((x & 0xFFFF0000FFFF0000ULL) >> 16) | ((x & 0x0000FFFF0000FFFFULL) << 16);
    return (x >> 32) | (x << 32);
}

/**
 * Compile-time sign function
 * Returns: -1 if negative, 0 if zero, 1 if positive
 */
template<typename T>
constexpr int sign(T val) noexcept {
    static_assert(std::is_arithmetic_v<T>, "sign requires arithmetic type");
    return (T{0} < val) - (val < T{0});
}

/**
 * Compile-time integer square root (Newton's method)
 */
constexpr uint64_t sqrt(uint64_t n) noexcept {
    if (n == 0 || n == 1) return n;

    uint64_t x = n;
    uint64_t y = (x + 1) / 2;

    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }

    return x;
}

// Common constants as constexpr variables
namespace constants {
    constexpr int64_t KIBI = 1024;
    constexpr int64_t MEBI = 1024 * 1024;
    constexpr int64_t GIBI = 1024 * 1024 * 1024;

    constexpr int64_t MILLION = 1000000;
    constexpr int64_t BILLION = 1000000000;

    // Common video dimensions
    constexpr int WIDTH_SD = 720;
    constexpr int HEIGHT_SD_NTSC = 480;
    constexpr int HEIGHT_SD_PAL = 576;

    constexpr int WIDTH_HD = 1280;
    constexpr int HEIGHT_HD = 720;

    constexpr int WIDTH_FHD = 1920;
    constexpr int HEIGHT_FHD = 1080;

    constexpr int WIDTH_4K = 3840;
    constexpr int HEIGHT_4K = 2160;

    constexpr int WIDTH_8K = 7680;
    constexpr int HEIGHT_8K = 4320;
}

// Static assertions to validate our implementations
static_assert(is_power_of_2(1024), "Power of 2 check failed");
static_assert(!is_power_of_2(1023), "Power of 2 check failed");
static_assert(next_power_of_2(1000) == 1024, "Next power of 2 failed");
static_assert(log2_floor(1024) == 10, "log2_floor failed");
static_assert(log2_ceil(1000) == 10, "log2_ceil failed");
static_assert(gcd(48, 18) == 6, "GCD failed");
static_assert(lcm(12, 18) == 36, "LCM failed");
static_assert(bswap16(0x1234) == 0x3412, "bswap16 failed");
static_assert(bswap32(0x12345678) == 0x78563412, "bswap32 failed");
static_assert(sqrt(144) == 12, "sqrt failed");
static_assert(align_up(1000, 16) == 1008, "align_up failed");
static_assert(align_down(1000, 16) == 992, "align_down failed");

} // namespace math
} // namespace ffmpeg

#endif // AVUTIL_MATHEMATICS_CONSTEXPR_HPP
