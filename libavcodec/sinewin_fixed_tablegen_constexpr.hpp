/*
 * Modern C++ constexpr fixed-point sine window tables
 * Copyright (c) 2009 Reimar Döffinger <Reimar.Doeffinger@gmx.de>
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
 * Modern C++20 constexpr fixed-point sine window tables
 *
 * This header provides compile-time generation of fixed-point (Q31) sine
 * window tables for audio codecs that use integer arithmetic.
 *
 * Sizes: 96, 120, 128, 480, 512, 768, 960, 1024
 * Total: 4,048 int32_t entries
 *
 * Fixed-point format: Q31 (signed 32-bit, 31 fractional bits)
 * Scale: sin(x) * 0x80000000 (range: -2^31 to 2^31-1)
 *
 * Used by fixed-point audio codecs for MDCT windowing.
 * Complements the floating-point sine windows from sinewin_tablegen.
 */

#ifndef AVCODEC_SINEWIN_FIXED_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_SINEWIN_FIXED_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>

namespace ffmpeg {
namespace sinewin_fixed {

// Reuse sine implementation from floating-point version
constexpr double PI = 3.14159265358979323846;

constexpr double sin_constexpr(double x) noexcept {
    // Normalize to [-π, π]
    while (x > PI) x -= 2.0 * PI;
    while (x < -PI) x += 2.0 * PI;

    // Taylor series (11 terms)
    double x2 = x * x;
    double result = x;
    double term = x;

    term *= -x2 / (2.0 * 3.0);
    result += term;

    term *= -x2 / (4.0 * 5.0);
    result += term;

    term *= -x2 / (6.0 * 7.0);
    result += term;

    term *= -x2 / (8.0 * 9.0);
    result += term;

    term *= -x2 / (10.0 * 11.0);
    result += term;

    term *= -x2 / (12.0 * 13.0);
    result += term;

    term *= -x2 / (14.0 * 15.0);
    result += term;

    term *= -x2 / (16.0 * 17.0);
    result += term;

    term *= -x2 / (18.0 * 19.0);
    result += term;

    term *= -x2 / (20.0 * 21.0);
    result += term;

    return result;
}

/**
 * Convert floating-point sine to Q31 fixed-point
 *
 * Formula: floor(sin(x) * 0x80000000 + 0.5)
 * Range: -2147483648 to 2147483647 (int32_t)
 */
constexpr int32_t sin_to_fixed_q31(double sine_val) noexcept {
    constexpr double Q31_SCALE = 2147483648.0;  // 2^31
    double scaled = sine_val * Q31_SCALE;

    // Round to nearest
    if (scaled >= 0.0) {
        scaled += 0.5;
    } else {
        scaled -= 0.5;
    }

    // Clamp to int32_t range
    if (scaled > 2147483647.0) return 2147483647;
    if (scaled < -2147483648.0) return -2147483648;

    return static_cast<int32_t>(scaled);
}

/**
 * Generate fixed-point sine window of size N
 *
 * Formula: window[i] = sin((i + 0.5) * π / (2N)) * 2^31
 *
 * @tparam N Window size
 * @return std::array<int32_t, N> containing Q31 fixed-point values
 */
template<size_t N>
constexpr auto generate_sine_window_fixed() noexcept {
    std::array<int32_t, N> window{};

    constexpr double factor = PI / (2.0 * static_cast<double>(N));

    for (size_t i = 0; i < N; ++i) {
        double angle = (static_cast<double>(i) + 0.5) * factor;
        double sine_val = sin_constexpr(angle);
        window[i] = sin_to_fixed_q31(sine_val);
    }

    return window;
}

// Generate all fixed-point window sizes at compile time
// These sizes are used by fixed-point audio codecs
constexpr auto sine_window_96_fixed = generate_sine_window_fixed<96>();
constexpr auto sine_window_120_fixed = generate_sine_window_fixed<120>();
constexpr auto sine_window_128_fixed = generate_sine_window_fixed<128>();
constexpr auto sine_window_480_fixed = generate_sine_window_fixed<480>();
constexpr auto sine_window_512_fixed = generate_sine_window_fixed<512>();
constexpr auto sine_window_768_fixed = generate_sine_window_fixed<768>();
constexpr auto sine_window_960_fixed = generate_sine_window_fixed<960>();
constexpr auto sine_window_1024_fixed = generate_sine_window_fixed<1024>();

// Total: 96 + 120 + 128 + 480 + 512 + 768 + 960 + 1024 = 4,048 entries!

// Compile-time validation
namespace tests {
    // Test table sizes
    static_assert(sine_window_96_fixed.size() == 96, "Window 96 size");
    static_assert(sine_window_120_fixed.size() == 120, "Window 120 size");
    static_assert(sine_window_128_fixed.size() == 128, "Window 128 size");
    static_assert(sine_window_480_fixed.size() == 480, "Window 480 size");
    static_assert(sine_window_512_fixed.size() == 512, "Window 512 size");
    static_assert(sine_window_768_fixed.size() == 768, "Window 768 size");
    static_assert(sine_window_960_fixed.size() == 960, "Window 960 size");
    static_assert(sine_window_1024_fixed.size() == 1024, "Window 1024 size");

    // Test Q31 conversion
    // sin(0) ≈ 0 → Q31 = 0
    constexpr int32_t q31_zero = sin_to_fixed_q31(0.0);
    static_assert(q31_zero == 0, "Q31 zero");

    // sin(π/2) = 1.0 → Q31 = 2^31 - 1 (max positive)
    constexpr int32_t q31_one = sin_to_fixed_q31(1.0);
    static_assert(q31_one == 2147483647, "Q31 one = 2^31-1");

    // sin(-π/2) = -1.0 → Q31 = -2^31 (max negative)
    constexpr int32_t q31_neg_one = sin_to_fixed_q31(-1.0);
    static_assert(q31_neg_one == -2147483648, "Q31 -1 = -2^31");

    // sin(π/6) = 0.5 → Q31 ≈ 2^30
    constexpr int32_t q31_half = sin_to_fixed_q31(0.5);
    static_assert(q31_half > 1073741823 && q31_half < 1073741825, "Q31 0.5 ≈ 2^30");

    // Test first values are small but positive (windows start near zero)
    static_assert(sine_window_96_fixed[0] > 0, "Window 96 first positive");
    static_assert(sine_window_128_fixed[0] > 0, "Window 128 first positive");
    static_assert(sine_window_1024_fixed[0] > 0, "Window 1024 first positive");

    // Test that values increase initially
    static_assert(sine_window_96_fixed[1] > sine_window_96_fixed[0], "Window 96 monotonic");
    static_assert(sine_window_128_fixed[1] > sine_window_128_fixed[0], "Window 128 monotonic");

    // Test last values approach 2^31-1 (sine approaches 1.0)
    constexpr int32_t max_q31 = 2147483647;
    static_assert(sine_window_96_fixed[95] > max_q31 - 10000000, "Window 96 last near max");
    static_assert(sine_window_128_fixed[127] > max_q31 - 5000000, "Window 128 last near max");
    static_assert(sine_window_1024_fixed[1023] > max_q31 - 1000000, "Window 1024 last near max");

    // Test mid-point values (should be around 0.707 * 2^31 ≈ 1.52e9)
    constexpr int64_t mid_point_approx = 1520000000LL;
    static_assert(sine_window_96_fixed[47] > mid_point_approx - 100000000 &&
                  sine_window_96_fixed[47] < mid_point_approx + 100000000,
                  "Window 96 midpoint");

    static_assert(sine_window_128_fixed[63] > mid_point_approx - 100000000 &&
                  sine_window_128_fixed[63] < mid_point_approx + 100000000,
                  "Window 128 midpoint");

    // Test monotonicity in several windows
    static_assert(sine_window_96_fixed[10] < sine_window_96_fixed[20], "Monotonic 96");
    static_assert(sine_window_128_fixed[10] < sine_window_128_fixed[20], "Monotonic 128");
    static_assert(sine_window_512_fixed[100] < sine_window_512_fixed[200], "Monotonic 512");

    // Test that different sized windows have different values at same index
    // (due to different scaling factor)
    static_assert(sine_window_96_fixed[10] != sine_window_128_fixed[10],
                  "Different windows differ");

    // Test sine function accuracy (from floating-point version)
    constexpr double test_sin_0 = sin_constexpr(0.0);
    static_assert(test_sin_0 > -0.001 && test_sin_0 < 0.001, "sin(0) ≈ 0");

    constexpr double test_sin_pi_6 = sin_constexpr(PI / 6.0);
    static_assert(test_sin_pi_6 > 0.499 && test_sin_pi_6 < 0.501, "sin(π/6) ≈ 0.5");

    constexpr double test_sin_pi_4 = sin_constexpr(PI / 4.0);
    static_assert(test_sin_pi_4 > 0.706 && test_sin_pi_4 < 0.708, "sin(π/4) ≈ 0.707");

    constexpr double test_sin_pi_2 = sin_constexpr(PI / 2.0);
    static_assert(test_sin_pi_2 > 0.999 && test_sin_pi_2 < 1.001, "sin(π/2) ≈ 1.0");

    // Test Q31 range boundaries
    static_assert(sin_to_fixed_q31(1.5) == max_q31, "Q31 clamp positive");
    static_assert(sin_to_fixed_q31(-1.5) == -2147483648, "Q31 clamp negative");

    // Test rounding behavior
    constexpr int32_t q31_round_up = sin_to_fixed_q31(0.0000000006);  // Should round up to 1
    static_assert(q31_round_up >= 1, "Q31 rounds up");

    constexpr int32_t q31_round_down = sin_to_fixed_q31(-0.0000000006);  // Should round down to -1
    static_assert(q31_round_down <= -1, "Q31 rounds down");
}

/**
 * Constexpr accessor for window data by size
 */
constexpr const int32_t* get_sine_window_fixed_by_size(int size) noexcept {
    switch (size) {
        case 96:   return sine_window_96_fixed.data();
        case 120:  return sine_window_120_fixed.data();
        case 128:  return sine_window_128_fixed.data();
        case 480:  return sine_window_480_fixed.data();
        case 512:  return sine_window_512_fixed.data();
        case 768:  return sine_window_768_fixed.data();
        case 960:  return sine_window_960_fixed.data();
        case 1024: return sine_window_1024_fixed.data();
        default:   return nullptr;
    }
}

/**
 * Helper to get window value at specific index
 */
constexpr int32_t get_sine_window_fixed_value(int size, int index) noexcept {
    const int32_t* window = get_sine_window_fixed_by_size(size);
    if (!window || index < 0 || index >= size) return 0;

    // Since we can't dereference pointer in constexpr easily,
    // use switch for common cases
    switch (size) {
        case 96:   return (index < 96) ? sine_window_96_fixed[index] : 0;
        case 120:  return (index < 120) ? sine_window_120_fixed[index] : 0;
        case 128:  return (index < 128) ? sine_window_128_fixed[index] : 0;
        case 480:  return (index < 480) ? sine_window_480_fixed[index] : 0;
        case 512:  return (index < 512) ? sine_window_512_fixed[index] : 0;
        case 768:  return (index < 768) ? sine_window_768_fixed[index] : 0;
        case 960:  return (index < 960) ? sine_window_960_fixed[index] : 0;
        case 1024: return (index < 1024) ? sine_window_1024_fixed[index] : 0;
        default:   return 0;
    }
}

} // namespace sinewin_fixed
} // namespace ffmpeg

#endif // AVCODEC_SINEWIN_FIXED_TABLEGEN_CONSTEXPR_HPP
