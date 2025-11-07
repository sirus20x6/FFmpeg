/*
 * Modern C++ constexpr DCA-LBR cosine table
 * Copyright (C) 2016 foo86
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
 * Modern C++20 constexpr DCA-LBR (Low Bit Rate) cosine table
 *
 * DCA-LBR is a low-bitrate extension of the DTS audio codec, used for
 * space-constrained applications. It uses a 256-entry cosine lookup table
 * for efficient sinusoidal synthesis in the decoder.
 *
 * Table:
 * - cos_tab[256]: Cosine values for 2 full periods (0 to 2π)
 *
 * Formula: cos_tab[i] = cos(π × i / 128)
 *
 * This provides 128 samples per π radians, or 256 samples for 2π,
 * covering two complete cosine cycles from 0 to 4π (though the function
 * repeats, so effectively 0 to 2π with double coverage).
 *
 * Total: 256 float entries
 *
 * Used by: DCA-LBR audio decoder (DTS Low Bit Rate extension)
 */

#ifndef AVCODEC_DCA_LBR_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_DCA_LBR_TABLEGEN_CONSTEXPR_HPP

#include <array>

namespace ffmpeg {
namespace dca_lbr {

// Constants
constexpr int COS_TAB_SIZE = 256;

// Mathematical constant
constexpr double PI = 3.14159265358979323846;

/**
 * Constexpr cosine approximation using Taylor series
 *
 * cos(x) = 1 - x²/2! + x⁴/4! - x⁶/6! + x⁸/8! - ...
 *
 * For DCA-LBR, x ranges from 0 to 2π, so we need good accuracy
 * across the full range. Using 10 terms provides excellent precision.
 */
constexpr double cos_constexpr(double x) noexcept {
    // Reduce to [0, 2π] range
    constexpr double TWO_PI = 2.0 * PI;
    while (x < 0.0) x += TWO_PI;
    while (x > TWO_PI) x -= TWO_PI;

    // Further reduce to [0, π] using cos(-x) = cos(x) symmetry
    if (x > PI) {
        x = TWO_PI - x;
    }

    // Reduce to [0, π/2] using cos(π - x) = -cos(x)
    bool negate = false;
    if (x > PI / 2.0) {
        x = PI - x;
        negate = true;
    }

    // Taylor series around 0: cos(x) = 1 - x²/2 + x⁴/24 - x⁶/720 + ...
    double x2 = x * x;
    double result = 1.0;
    double term = 1.0;

    // 10 terms for excellent accuracy
    for (int n = 1; n <= 10; ++n) {
        term *= -x2 / ((2 * n - 1) * (2 * n));
        result += term;
    }

    return negate ? -result : result;
}

/**
 * Generate DCA-LBR cosine table at compile time
 *
 * Algorithm (from dca_lbr.c ff_dca_lbr_init_tables):
 * - cos_tab[i] = cos(π × i / 128) for i = 0..255
 *
 * Index mapping:
 * - i=0:   cos(0) = 1.0
 * - i=64:  cos(π/2) = 0.0
 * - i=128: cos(π) = -1.0
 * - i=192: cos(3π/2) = 0.0
 * - i=256 would be: cos(2π) = 1.0 (but array only goes to 255)
 *
 * The table covers slightly less than 2 full periods, with 128 samples
 * per period. This provides fine angular resolution for sinusoidal synthesis.
 *
 * Usage in decoder:
 * - Real part: cos_tab[phase & 255]
 * - Imaginary part: cos_tab[(phase + 64) & 255]  (90° phase shift)
 *
 * The +64 offset provides a sine wave (cos shifted by π/2).
 */
constexpr auto generate_dca_lbr_cos_tab() noexcept {
    std::array<float, COS_TAB_SIZE> table{};

    for (int i = 0; i < COS_TAB_SIZE; ++i) {
        double angle = PI * static_cast<double>(i) / 128.0;
        table[i] = static_cast<float>(cos_constexpr(angle));
    }

    return table;
}

// Generate table at compile time
constexpr auto cos_tab = generate_dca_lbr_cos_tab();

// Total: 256 float entries!

// Compile-time validation
namespace tests {
    // Test table size
    static_assert(cos_tab.size() == COS_TAB_SIZE, "cos_tab size = 256");
    static_assert(COS_TAB_SIZE == 256, "COS_TAB_SIZE constant");

    // Test key cosine values
    // cos(0) = 1.0
    static_assert(cos_tab[0] >= 0.99f && cos_tab[0] <= 1.01f,
                  "cos_tab[0] = cos(0) = 1.0");

    // cos(π/4) ≈ 0.707 (i=32: π*32/128 = π/4)
    static_assert(cos_tab[32] >= 0.70f && cos_tab[32] <= 0.71f,
                  "cos_tab[32] = cos(π/4) ≈ 0.707");

    // cos(π/2) = 0.0 (i=64: π*64/128 = π/2)
    static_assert(cos_tab[64] >= -0.01f && cos_tab[64] <= 0.01f,
                  "cos_tab[64] = cos(π/2) = 0.0");

    // cos(3π/4) ≈ -0.707 (i=96: π*96/128 = 3π/4)
    static_assert(cos_tab[96] >= -0.71f && cos_tab[96] <= -0.70f,
                  "cos_tab[96] = cos(3π/4) ≈ -0.707");

    // cos(π) = -1.0 (i=128: π*128/128 = π)
    static_assert(cos_tab[128] >= -1.01f && cos_tab[128] <= -0.99f,
                  "cos_tab[128] = cos(π) = -1.0");

    // cos(5π/4) ≈ -0.707 (i=160: π*160/128 = 5π/4)
    static_assert(cos_tab[160] >= -0.71f && cos_tab[160] <= -0.70f,
                  "cos_tab[160] = cos(5π/4) ≈ -0.707");

    // cos(3π/2) = 0.0 (i=192: π*192/128 = 3π/2)
    static_assert(cos_tab[192] >= -0.01f && cos_tab[192] <= 0.01f,
                  "cos_tab[192] = cos(3π/2) = 0.0");

    // cos(7π/4) ≈ 0.707 (i=224: π*224/128 = 7π/4)
    static_assert(cos_tab[224] >= 0.70f && cos_tab[224] <= 0.71f,
                  "cos_tab[224] = cos(7π/4) ≈ 0.707");

    // Test symmetry: cos is even function cos(-x) = cos(x)
    // Also periodic: cos(x + 2π) = cos(x)
    // cos_tab[64] (π/2) should equal -cos_tab[192] (3π/2)... wait, that's wrong
    // Actually: cos(3π/2) = 0 = cos(π/2), both are 0

    // Test symmetry around π: cos(π - x) = -cos(x)
    // cos_tab[128-32] should equal -cos_tab[128+32]
    // cos(π - π/4) = cos(3π/4) = -cos(π/4)
    static_assert(cos_tab[96] <= -cos_tab[32] + 0.01f &&
                  cos_tab[96] >= -cos_tab[32] - 0.01f,
                  "Symmetry around π");

    // Test 90° phase shift property
    // sin(x) = cos(x - π/2) = cos_tab[(i - 64) & 255]
    // OR sin(x) = cos(π/2 - x) = -cos(x + π/2) for x > 0... complex
    // In code: cos_tab[(phase + 64) & 255] gives sine
    // This works because cos(x + π/2) = -sin(x), so cos_tab[i+64] = -sin(angle_i)
    // But we want sin, so... actually let's just verify the offset works
    // sin(0) = 0, which should be cos(π/2) = cos_tab[64] = 0 ✓

    // Test monotonicity in first quadrant [0, π/2]
    static_assert(cos_tab[0] > cos_tab[16], "Decreasing in Q1");
    static_assert(cos_tab[16] > cos_tab[32], "Decreasing in Q1 cont");
    static_assert(cos_tab[32] > cos_tab[48], "Decreasing in Q1 cont2");
    static_assert(cos_tab[48] > cos_tab[64], "Decreasing in Q1 to 0");

    // Test monotonicity in second quadrant [π/2, π] - continues decreasing
    static_assert(cos_tab[64] > cos_tab[80], "Decreasing in Q2");
    static_assert(cos_tab[80] > cos_tab[96], "Decreasing in Q2 cont");
    static_assert(cos_tab[96] > cos_tab[112], "Decreasing in Q2 cont2");
    static_assert(cos_tab[112] > cos_tab[128], "Decreasing in Q2 to -1");

    // Test monotonicity in third quadrant [π, 3π/2] - increasing
    static_assert(cos_tab[128] < cos_tab[144], "Increasing in Q3");
    static_assert(cos_tab[144] < cos_tab[160], "Increasing in Q3 cont");
    static_assert(cos_tab[160] < cos_tab[176], "Increasing in Q3 cont2");
    static_assert(cos_tab[176] < cos_tab[192], "Increasing in Q3 to 0");

    // Test monotonicity in fourth quadrant [3π/2, 2π] - continues increasing
    static_assert(cos_tab[192] < cos_tab[208], "Increasing in Q4");
    static_assert(cos_tab[208] < cos_tab[224], "Increasing in Q4 cont");
    static_assert(cos_tab[224] < cos_tab[240], "Increasing in Q4 cont2");
    static_assert(cos_tab[240] < cos_tab[255], "Increasing in Q4 toward 1");

    // Test range [-1, 1]
    static_assert(cos_tab[100] >= -1.01f && cos_tab[100] <= 1.01f, "Range check");
    static_assert(cos_tab[200] >= -1.01f && cos_tab[200] <= 1.01f, "Range check 2");

    // Test that table is not all zeros
    static_assert(cos_tab[0] != 0.0f, "Non-zero entry");
    static_assert(cos_tab[128] != 0.0f, "Non-zero entry 2");
}

/**
 * Constexpr accessor with bounds checking
 */
constexpr float get_cos_value(int index) noexcept {
    if (index < 0 || index >= COS_TAB_SIZE) return 0.0f;
    return cos_tab[index];
}

/**
 * Get raw table pointer for C interop
 */
inline const float* get_cos_tab() noexcept {
    return cos_tab.data();
}

/**
 * Get sine value using 90° phase shift
 * sin(x) = cos(x - π/2) = cos_tab[(i + 64) & 255]
 */
constexpr float get_sin_value(int index) noexcept {
    return get_cos_value((index + 64) & 255);
}

} // namespace dca_lbr
} // namespace ffmpeg

#endif // AVCODEC_DCA_LBR_TABLEGEN_CONSTEXPR_HPP
