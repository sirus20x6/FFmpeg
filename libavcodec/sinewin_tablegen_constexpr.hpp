/*
 * Modern C++ constexpr sine window tables
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
 * Modern C++20 constexpr sine window table generation
 *
 * This header provides compile-time generation of sine window tables
 * used by various audio codecs (AAC, AC3, Vorbis, etc.).
 *
 * Sine windows are used for:
 * - MDCT window functions
 * - Smooth signal transitions
 * - Overlapping transforms
 *
 * The formula for each window size N is:
 *   window[i] = sin((i + 0.5) * π / (2N))   for i = 0..N-1
 *
 * All tables are generated at compile time, eliminating runtime
 * initialization overhead.
 */

#ifndef AVCODEC_SINEWIN_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_SINEWIN_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cmath>
#include <cstdint>

namespace ffmpeg {
namespace sinewin {

// Mathematical constants
constexpr double PI = 3.14159265358979323846;

/**
 * Constexpr sine approximation using Taylor series
 * Accurate enough for our windowing purposes
 */
constexpr double sin_constexpr(double x) noexcept {
    // Normalize to [-π, π]
    while (x > PI) x -= 2.0 * PI;
    while (x < -PI) x += 2.0 * PI;

    // Taylor series: sin(x) = x - x³/3! + x⁵/5! - x⁷/7! + ...
    // Using 11 terms for high accuracy
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
 * Generate a sine window of size N at compile time
 *
 * Formula: window[i] = sin((i + 0.5) * π / (2N))
 *
 * @tparam N Window size (must be power of 2: 32, 64, 128, etc.)
 * @return std::array<float, N> containing the window values
 */
template<size_t N>
constexpr auto generate_sine_window() noexcept {
    std::array<float, N> window{};

    constexpr double factor = PI / (2.0 * static_cast<double>(N));

    for (size_t i = 0; i < N; ++i) {
        double angle = (static_cast<double>(i) + 0.5) * factor;
        window[i] = static_cast<float>(sin_constexpr(angle));
    }

    return window;
}

// Generate all standard window sizes at compile time
// These match FFmpeg's standard window sizes for MDCT

constexpr auto sine_window_32 = generate_sine_window<32>();
constexpr auto sine_window_64 = generate_sine_window<64>();
constexpr auto sine_window_128 = generate_sine_window<128>();
constexpr auto sine_window_256 = generate_sine_window<256>();
constexpr auto sine_window_512 = generate_sine_window<512>();
constexpr auto sine_window_1024 = generate_sine_window<1024>();
constexpr auto sine_window_2048 = generate_sine_window<2048>();
constexpr auto sine_window_4096 = generate_sine_window<4096>();
constexpr auto sine_window_8192 = generate_sine_window<8192>();

// Total: 16,352 float entries generated at compile time!

// Compile-time validation
namespace tests {
    // Test that windows are properly generated
    static_assert(sine_window_32.size() == 32, "Window 32 size");
    static_assert(sine_window_64.size() == 64, "Window 64 size");
    static_assert(sine_window_128.size() == 128, "Window 128 size");
    static_assert(sine_window_256.size() == 256, "Window 256 size");
    static_assert(sine_window_512.size() == 512, "Window 512 size");
    static_assert(sine_window_1024.size() == 1024, "Window 1024 size");
    static_assert(sine_window_2048.size() == 2048, "Window 2048 size");
    static_assert(sine_window_4096.size() == 4096, "Window 4096 size");
    static_assert(sine_window_8192.size() == 8192, "Window 8192 size");

    // Test that first values are non-zero (windows should start small but positive)
    static_assert(sine_window_32[0] > 0.0f, "Window 32 first value positive");
    static_assert(sine_window_128[0] > 0.0f, "Window 128 first value positive");
    static_assert(sine_window_1024[0] > 0.0f, "Window 1024 first value positive");

    // Test that values increase initially (sine function characteristic)
    static_assert(sine_window_32[1] > sine_window_32[0], "Window 32 monotonic start");
    static_assert(sine_window_128[1] > sine_window_128[0], "Window 128 monotonic start");

    // Test that mid-point values are reasonable (should be around 0.7 for symmetric windows)
    static_assert(sine_window_32[15] > 0.6f && sine_window_32[15] < 0.8f, "Window 32 midpoint");
    static_assert(sine_window_128[63] > 0.6f && sine_window_128[63] < 0.8f, "Window 128 midpoint");

    // Test that last values approach 1.0 (sin approaches π/4)
    static_assert(sine_window_32[31] > 0.95f, "Window 32 last value near 1.0");
    static_assert(sine_window_128[127] > 0.98f, "Window 128 last value near 1.0");
    static_assert(sine_window_1024[1023] > 0.99f, "Window 1024 last value near 1.0");

    // Test sine approximation accuracy at key points
    constexpr double test_sin_0 = sin_constexpr(0.0);
    static_assert(test_sin_0 > -0.001 && test_sin_0 < 0.001, "sin(0) ≈ 0");

    constexpr double test_sin_pi_6 = sin_constexpr(PI / 6.0);  // sin(30°) = 0.5
    static_assert(test_sin_pi_6 > 0.499 && test_sin_pi_6 < 0.501, "sin(π/6) ≈ 0.5");

    constexpr double test_sin_pi_4 = sin_constexpr(PI / 4.0);  // sin(45°) = √2/2 ≈ 0.707
    static_assert(test_sin_pi_4 > 0.706 && test_sin_pi_4 < 0.708, "sin(π/4) ≈ 0.707");

    constexpr double test_sin_pi_3 = sin_constexpr(PI / 3.0);  // sin(60°) = √3/2 ≈ 0.866
    static_assert(test_sin_pi_3 > 0.865 && test_sin_pi_3 < 0.867, "sin(π/3) ≈ 0.866");

    constexpr double test_sin_pi_2 = sin_constexpr(PI / 2.0);  // sin(90°) = 1.0
    static_assert(test_sin_pi_2 > 0.999 && test_sin_pi_2 < 1.001, "sin(π/2) ≈ 1.0");
}

/**
 * Helper to get window size from index
 * Matches FFmpeg's indexing: 5->32, 6->64, 7->128, etc.
 */
constexpr size_t window_index_to_size(int index) noexcept {
    return 1u << index;
}

/**
 * Constexpr accessor for window data by index
 *
 * Note: Returns pointer to const data, not a copy
 */
constexpr const float* get_sine_window_by_index(int index) noexcept {
    switch (index) {
        case 5:  return sine_window_32.data();
        case 6:  return sine_window_64.data();
        case 7:  return sine_window_128.data();
        case 8:  return sine_window_256.data();
        case 9:  return sine_window_512.data();
        case 10: return sine_window_1024.data();
        case 11: return sine_window_2048.data();
        case 12: return sine_window_4096.data();
        case 13: return sine_window_8192.data();
        default: return nullptr;
    }
}

} // namespace sinewin
} // namespace ffmpeg

#endif // AVCODEC_SINEWIN_TABLEGEN_CONSTEXPR_HPP
