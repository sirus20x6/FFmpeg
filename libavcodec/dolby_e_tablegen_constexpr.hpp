/*
 * Modern C++ constexpr Dolby E decoder tables
 * Copyright (c) 2014 Reimar Döffinger
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
 * Modern C++20 constexpr Dolby E audio codec decoder tables
 *
 * Dolby E is a professional audio codec used for multi-channel distribution in
 * broadcast and cinema applications. It requires several mantissa and exponent
 * scaling tables for quantization and dequantization.
 *
 * Tables:
 * 1. mantissa_tab1[17][4]: Primary mantissa scaling (power-of-2 patterns)
 * 2. mantissa_tab2[17][4]: Secondary mantissa (fractional scaling of tab1)
 * 3. mantissa_tab3[17][4]: Tertiary mantissa (sum-of-reciprocals)
 * 4. exponent_tab[50]: Power-of-2 exponents with √2 alternation
 * 5. gain_tab[1024]: Exponential gain scaling
 *
 * Total: 204 + 50 + 1,024 = 1,278 float entries
 *
 * Algorithms:
 * - mantissa_tab1: Power-of-2 divisions and shifted reciprocals
 * - mantissa_tab2: Fractional multipliers (0.5, 0.75, 0.875) of tab1
 * - mantissa_tab3: Sum-of-reciprocals: 1/(2^i) + 1/(2^j) - 1/(2^(i+j))
 * - exponent_tab: Alternating 2^(-i) and 2^(-i-0.5)
 * - gain_tab: exp2((i-960)/64) for dynamic range scaling
 *
 * Used by: Dolby E decoder (professional broadcast/cinema audio)
 */

#ifndef AVCODEC_DOLBY_E_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_DOLBY_E_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>

namespace ffmpeg {
namespace dolby_e {

// Constants
constexpr int MANTISSA_ROWS = 17;
constexpr int MANTISSA_COLS = 4;
constexpr int EXPONENT_SIZE = 50;
constexpr int GAIN_SIZE = 1024;

// Mathematical constants
constexpr float SQRT1_2_VAL = 0.70710678118654752440f;  // 1/√2 = √2/2

/**
 * Constexpr exp2 function for gain table
 *
 * Uses binary exponentiation for integer part + Taylor series for fraction.
 * For Dolby E gain table, arguments range from (0-960)/64 to (1023-960)/64,
 * i.e., approximately -15 to +0.984375.
 *
 * Uses exp(x*ln2) Taylor series: e^y = 1 + y + y²/2! + y³/3! + ...
 */
constexpr float exp2_constexpr(float x) noexcept {
    // Handle negative exponents
    bool negative = x < 0.0f;
    if (negative) x = -x;

    // Split into integer and fractional parts
    int int_part = static_cast<int>(x);
    float frac = x - static_cast<float>(int_part);

    // Compute 2^int_part using bit shift approximation
    float int_result = 1.0f;
    for (int i = 0; i < int_part; ++i) {
        int_result *= 2.0f;
    }

    // Compute 2^frac = e^(frac*ln2) using Taylor series (more accurate)
    constexpr float LN2 = 0.693147180559945309f;
    float y = frac * LN2;

    // Taylor series: e^y = 1 + y + y²/2 + y³/6 + y⁴/24 + y⁵/120 + y⁶/720
    float y2 = y * y;
    float y3 = y2 * y;
    float y4 = y2 * y2;
    float y5 = y4 * y;
    float y6 = y3 * y3;

    float frac_result = 1.0f + y + y2/2.0f + y3/6.0f + y4/24.0f + y5/120.0f + y6/720.0f;

    float result = int_result * frac_result;
    return negative ? (1.0f / result) : result;
}

/**
 * Generate mantissa_tab1 at compile time
 *
 * Algorithm (from dolby_e.c init_tables):
 * - [i][0] = 1.0 / (1 << (i-1)) for i=1..16  (simple power-of-2 reciprocal)
 * - [i][1] = 1.0 / ((1 << i) - 1) for i=2..15  (reciprocal of (2^i - 1))
 * - [i][2] = 0.5 / ((1 << i) - 1) for i=2..15
 * - [i][3] = 0.25 / ((1 << i) - 1) for i=2..15
 * - Special case at i=16:
 *   [16][1] = 0.5 / (1 << 15) = 0.5 / 32768
 *   [16][2] = 0.75 / (1 << 15)
 *   [16][3] = 0.875 / (1 << 15)
 *
 * Mathematical meaning: Provides scaling factors for different mantissa
 * bit depths (1-16 bits), with fractional variants (1.0, 0.5, 0.25).
 */
constexpr auto generate_mantissa_tab1() noexcept {
    std::array<std::array<float, MANTISSA_COLS>, MANTISSA_ROWS> table{};

    // Row 0 is not used (remains zero-initialized)

    for (int i = 1; i < 17; ++i) {
        // Column 0: 1.0 / (2^(i-1))
        table[i][0] = 1.0f / static_cast<float>(1 << (i - 1));
    }

    for (int i = 2; i < 16; ++i) {
        float divisor = static_cast<float>((1 << i) - 1);
        table[i][1] = 1.0f / divisor;
        table[i][2] = 0.5f / divisor;
        table[i][3] = 0.25f / divisor;
    }

    // Special case at i=16
    constexpr float div_16 = static_cast<float>(1 << 15);  // 32768
    table[16][1] = 0.5f / div_16;
    table[16][2] = 0.75f / div_16;
    table[16][3] = 0.875f / div_16;

    return table;
}

/**
 * Generate mantissa_tab2 at compile time
 *
 * Algorithm:
 * - [i][1] = mantissa_tab1[i][0] * 0.5
 * - [i][2] = mantissa_tab1[i][0] * 0.75
 * - [i][3] = mantissa_tab1[i][0] * 0.875
 *
 * Provides fractional scaling of the base power-of-2 values.
 */
constexpr auto generate_mantissa_tab2() noexcept {
    constexpr auto tab1 = generate_mantissa_tab1();
    std::array<std::array<float, MANTISSA_COLS>, MANTISSA_ROWS> table{};

    for (int i = 1; i < 17; ++i) {
        table[i][1] = tab1[i][0] * 0.5f;
        table[i][2] = tab1[i][0] * 0.75f;
        table[i][3] = tab1[i][0] * 0.875f;
    }

    return table;
}

/**
 * Generate mantissa_tab3 at compile time
 *
 * Algorithm:
 * - [i][j] = 1/(2^i) + 1/(2^j) - 1/(2^(i+j)) for i=1..16, j=1..3
 * - Special case: mantissa_tab3[1][3] = 0.6875
 *
 * Mathematical meaning: Sum-of-reciprocals with correction term.
 * This computes: (2^j + 2^i - 1) / 2^(i+j)
 */
constexpr auto generate_mantissa_tab3() noexcept {
    std::array<std::array<float, MANTISSA_COLS>, MANTISSA_ROWS> table{};

    for (int i = 1; i < 17; ++i) {
        for (int j = 1; j < 4; ++j) {
            float term1 = 1.0f / static_cast<float>(1 << i);
            float term2 = 1.0f / static_cast<float>(1 << j);
            float term3 = 1.0f / static_cast<float>(1 << (i + j));
            table[i][j] = term1 + term2 - term3;
        }
    }

    // Special override
    table[1][3] = 0.6875f;

    return table;
}

/**
 * Generate exponent table at compile time
 *
 * Algorithm:
 * - exponent_tab[i*2] = 1.0 / (1 << i) = 2^(-i)
 * - exponent_tab[i*2+1] = √(1/2) / (1 << i) = 2^(-i-0.5)
 *
 * For i=0..24, creates alternating sequence of 2^(-i) and 2^(-i-0.5).
 * Similar pattern to COOK rootpow2tab.
 *
 * Result: [1.0, √0.5, 0.5, √0.5/2, 0.25, √0.5/4, ...]
 */
constexpr auto generate_exponent_tab() noexcept {
    std::array<float, EXPONENT_SIZE> table{};

    for (int i = 0; i < 25; ++i) {
        float pow2_i = 1.0f / static_cast<float>(1 << i);
        table[i * 2] = pow2_i;
        table[i * 2 + 1] = SQRT1_2_VAL * pow2_i;
    }

    return table;
}

/**
 * Generate gain table at compile time
 *
 * Algorithm:
 * - gain_tab[i] = 2^((i-960)/64) for i=0..1023
 *
 * Range analysis:
 * - i=0: 2^(-960/64) = 2^(-15) ≈ 3.05e-5  (very small)
 * - i=960: 2^0 = 1.0  (unity gain)
 * - i=1023: 2^(63/64) ≈ 1.98  (near 2.0)
 *
 * Covers approximately -90 dB to +6 dB in 64 steps per doubling.
 * Used for fine-grained dynamic range control.
 */
constexpr auto generate_gain_tab() noexcept {
    std::array<float, GAIN_SIZE> table{};

    // Special case: gain_tab[0] should remain 0 (not computed)
    for (int i = 1; i < GAIN_SIZE; ++i) {
        float exponent = static_cast<float>(i - 960) / 64.0f;
        table[i] = exp2_constexpr(exponent);
    }

    return table;
}

// Generate all tables at compile time
constexpr auto mantissa_tab1 = generate_mantissa_tab1();
constexpr auto mantissa_tab2 = generate_mantissa_tab2();
constexpr auto mantissa_tab3 = generate_mantissa_tab3();
constexpr auto exponent_tab = generate_exponent_tab();
constexpr auto gain_tab = generate_gain_tab();

// Total: 17×4 + 17×4 + 17×4 + 50 + 1024 = 204 + 50 + 1024 = 1,278 float entries!

// Compile-time validation
namespace tests {
    // Test table sizes
    static_assert(mantissa_tab1.size() == MANTISSA_ROWS, "mantissa_tab1 rows");
    static_assert(mantissa_tab1[0].size() == MANTISSA_COLS, "mantissa_tab1 cols");
    static_assert(mantissa_tab2.size() == MANTISSA_ROWS, "mantissa_tab2 rows");
    static_assert(mantissa_tab3.size() == MANTISSA_ROWS, "mantissa_tab3 rows");
    static_assert(exponent_tab.size() == EXPONENT_SIZE, "exponent_tab size");
    static_assert(gain_tab.size() == GAIN_SIZE, "gain_tab size");

    // Test mantissa_tab1 properties
    // mantissa_tab1[1][0] = 1.0 / (2^0) = 1.0
    static_assert(mantissa_tab1[1][0] >= 0.99f && mantissa_tab1[1][0] <= 1.01f,
                  "mantissa_tab1[1][0] = 1.0");

    // mantissa_tab1[2][0] = 1.0 / (2^1) = 0.5
    static_assert(mantissa_tab1[2][0] >= 0.49f && mantissa_tab1[2][0] <= 0.51f,
                  "mantissa_tab1[2][0] = 0.5");

    // mantissa_tab1[16][0] = 1.0 / (2^15) ≈ 3.05e-5
    static_assert(mantissa_tab1[16][0] > 0.0f && mantissa_tab1[16][0] < 0.0001f,
                  "mantissa_tab1[16][0] very small");

    // mantissa_tab1[2][1] = 1.0 / ((2^2) - 1) = 1.0 / 3 ≈ 0.333
    static_assert(mantissa_tab1[2][1] >= 0.33f && mantissa_tab1[2][1] <= 0.34f,
                  "mantissa_tab1[2][1] ≈ 1/3");

    // mantissa_tab1[2][2] = 0.5 / 3 ≈ 0.167
    static_assert(mantissa_tab1[2][2] >= 0.16f && mantissa_tab1[2][2] <= 0.17f,
                  "mantissa_tab1[2][2] ≈ 1/6");

    // Special case: mantissa_tab1[16][1] = 0.5 / 32768
    static_assert(mantissa_tab1[16][1] > 0.0f && mantissa_tab1[16][1] < 0.00002f,
                  "mantissa_tab1[16][1] = 0.5/32768");

    // Test mantissa_tab2 properties
    // mantissa_tab2[1][1] = mantissa_tab1[1][0] * 0.5 = 1.0 * 0.5 = 0.5
    static_assert(mantissa_tab2[1][1] >= 0.49f && mantissa_tab2[1][1] <= 0.51f,
                  "mantissa_tab2[1][1] = 0.5");

    // mantissa_tab2[1][2] = mantissa_tab1[1][0] * 0.75 = 0.75
    static_assert(mantissa_tab2[1][2] >= 0.74f && mantissa_tab2[1][2] <= 0.76f,
                  "mantissa_tab2[1][2] = 0.75");

    // mantissa_tab2[1][3] = mantissa_tab1[1][0] * 0.875 = 0.875
    static_assert(mantissa_tab2[1][3] >= 0.87f && mantissa_tab2[1][3] <= 0.88f,
                  "mantissa_tab2[1][3] = 0.875");

    // Test mantissa_tab3 properties
    // mantissa_tab3[1][1] = 1/2 + 1/2 - 1/4 = 0.75
    static_assert(mantissa_tab3[1][1] >= 0.74f && mantissa_tab3[1][1] <= 0.76f,
                  "mantissa_tab3[1][1] = 0.75");

    // mantissa_tab3[1][3] = 0.6875 (special override)
    static_assert(mantissa_tab3[1][3] >= 0.68f && mantissa_tab3[1][3] <= 0.69f,
                  "mantissa_tab3[1][3] = 0.6875");

    // mantissa_tab3[2][2] = 1/4 + 1/4 - 1/16 = 0.4375
    static_assert(mantissa_tab3[2][2] >= 0.43f && mantissa_tab3[2][2] <= 0.44f,
                  "mantissa_tab3[2][2] ≈ 0.4375");

    // Test exponent_tab properties
    // exponent_tab[0] = 2^0 = 1.0
    static_assert(exponent_tab[0] >= 0.99f && exponent_tab[0] <= 1.01f,
                  "exponent_tab[0] = 1.0");

    // exponent_tab[1] = √0.5 ≈ 0.707
    static_assert(exponent_tab[1] >= 0.70f && exponent_tab[1] <= 0.71f,
                  "exponent_tab[1] = √0.5");

    // exponent_tab[2] = 2^(-1) = 0.5
    static_assert(exponent_tab[2] >= 0.49f && exponent_tab[2] <= 0.51f,
                  "exponent_tab[2] = 0.5");

    // exponent_tab[3] = √0.5 / 2 ≈ 0.354
    static_assert(exponent_tab[3] >= 0.35f && exponent_tab[3] <= 0.36f,
                  "exponent_tab[3] = √0.5/2");

    // Test alternating pattern: exponent_tab[i*2+1] / exponent_tab[i*2] ≈ √0.5
    static_assert(exponent_tab[1] / exponent_tab[0] >= 0.70f &&
                  exponent_tab[1] / exponent_tab[0] <= 0.71f,
                  "Alternating √0.5 pattern");

    // Test gain_tab properties
    // gain_tab[0] = 0.0 (special case, not computed)
    static_assert(gain_tab[0] == 0.0f, "gain_tab[0] = 0");

    // gain_tab[960] = 2^0 = 1.0 (unity gain)
    static_assert(gain_tab[960] >= 0.99f && gain_tab[960] <= 1.01f,
                  "gain_tab[960] = 1.0");

    // gain_tab[1] = 2^((1-960)/64) = 2^(-14.984) ≈ 3.07e-5 (very small)
    static_assert(gain_tab[1] > 0.0f && gain_tab[1] < 0.0001f,
                  "gain_tab[1] very small");

    // gain_tab[1023] = 2^((1023-960)/64) = 2^(0.984) ≈ 1.97
    static_assert(gain_tab[1023] >= 1.90f && gain_tab[1023] <= 2.0f,
                  "gain_tab[1023] ≈ 2.0");

    // Test monotonicity: gain should increase
    static_assert(gain_tab[100] < gain_tab[200], "Gain monotonic 1");
    static_assert(gain_tab[500] < gain_tab[960], "Gain monotonic 2");
    static_assert(gain_tab[960] < gain_tab[1000], "Gain monotonic 3");

    // Test doubling: gain_tab[896] ≈ 0.5 * gain_tab[960]
    // Since 2^((896-960)/64) = 2^(-1) = 0.5
    static_assert(gain_tab[896] / gain_tab[960] >= 0.49f &&
                  gain_tab[896] / gain_tab[960] <= 0.51f,
                  "Gain doubles every 64 steps");
}

/**
 * Constexpr accessors with bounds checking
 */
constexpr float get_mantissa_tab1_value(int i, int j) noexcept {
    if (i < 0 || i >= MANTISSA_ROWS || j < 0 || j >= MANTISSA_COLS) return 0.0f;
    return mantissa_tab1[i][j];
}

constexpr float get_mantissa_tab2_value(int i, int j) noexcept {
    if (i < 0 || i >= MANTISSA_ROWS || j < 0 || j >= MANTISSA_COLS) return 0.0f;
    return mantissa_tab2[i][j];
}

constexpr float get_mantissa_tab3_value(int i, int j) noexcept {
    if (i < 0 || i >= MANTISSA_ROWS || j < 0 || j >= MANTISSA_COLS) return 0.0f;
    return mantissa_tab3[i][j];
}

constexpr float get_exponent_value(int index) noexcept {
    if (index < 0 || index >= EXPONENT_SIZE) return 0.0f;
    return exponent_tab[index];
}

constexpr float get_gain_value(int index) noexcept {
    if (index < 0 || index >= GAIN_SIZE) return 0.0f;
    return gain_tab[index];
}

/**
 * Get raw table pointers for C interop
 */
inline const float (*get_mantissa_tab1())[MANTISSA_COLS] {
    return reinterpret_cast<const float(*)[MANTISSA_COLS]>(mantissa_tab1.data());
}

inline const float (*get_mantissa_tab2())[MANTISSA_COLS] {
    return reinterpret_cast<const float(*)[MANTISSA_COLS]>(mantissa_tab2.data());
}

inline const float (*get_mantissa_tab3())[MANTISSA_COLS] {
    return reinterpret_cast<const float(*)[MANTISSA_COLS]>(mantissa_tab3.data());
}

inline const float* get_exponent_tab() noexcept {
    return exponent_tab.data();
}

inline const float* get_gain_tab() noexcept {
    return gain_tab.data();
}

} // namespace dolby_e
} // namespace ffmpeg

#endif // AVCODEC_DOLBY_E_TABLEGEN_CONSTEXPR_HPP
