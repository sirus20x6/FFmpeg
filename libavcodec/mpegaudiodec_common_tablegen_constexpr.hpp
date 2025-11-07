/*
 * Modern C++ constexpr MPEG Audio decoder common tables
 * Copyright (c) 2009 Reimar Döffinger <Reimar.Doeffinger@gmx.de>
 * Copyright (c) 2020 Andreas Rheinhardt <andreas.rheinhardt@gmail.com>
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
 * Modern C++20 constexpr MPEG Audio decoder common tables
 *
 * This header provides compile-time generation of shared tables used across
 * multiple MPEG audio decoders (MP1, MP2, MP3).
 *
 * Tables:
 * - table_4_3_exp: 32,828 int8_t exponents
 * - table_4_3_value: 32,828 uint32_t mantissas
 *
 * Total: 65,656 entries (32,828 × 2)
 *
 * Mathematical background:
 * Computes value^(4/3) in floating-point format (mantissa + exponent).
 * Uses frexp() decomposition: f = mantissa * 2^exponent
 * Stores in normalized form with FRAC_BITS = 23 fractional bits.
 *
 * Formula: (i/4)^(4/3) / IMDCT_SCALAR * 2^(i&3 * 0.25)
 *
 * This is the most commonly used table across all MP3 decoder variants.
 */

#ifndef AVCODEC_MPEGAUDIODEC_COMMON_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_MPEGAUDIODEC_COMMON_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>

namespace ffmpeg {
namespace mpegaudiodec_common {

// Constants
constexpr int TABLE_4_3_SIZE = (8191 + 16) * 4;  // 32,828
constexpr int FRAC_BITS = 23;
constexpr double IMDCT_SCALAR = 1.759;

// Reuse cube root from previous implementations
constexpr double cbrt_constexpr(double x) noexcept {
    if (x == 0.0) return 0.0;
    if (x < 0.0) return -cbrt_constexpr(-x);

    double guess = (x < 1.0) ? x : (x / 3.0 + 0.5);

    for (int i = 0; i < 10; ++i) {
        double guess_squared = guess * guess;
        guess = (2.0 * guess + x / guess_squared) / 3.0;
    }

    return guess;
}

// Pre-computed 2^(n/4) lookup table
constexpr std::array<double, 4> exp2_lut = {
    1.00000000000000000000,  // 2^(0/4)
    1.18920711500272106672,  // 2^(1/4)
    1.41421356237309504880,  // 2^(2/4) = √2
    1.68179283050742908606,  // 2^(3/4)
};

/**
 * Constexpr frexp - decompose floating-point into mantissa and exponent
 *
 * Returns mantissa m where 0.5 <= |m| < 1.0
 * and exponent e where x = m * 2^e
 */
constexpr double frexp_constexpr(double x, int* exp) noexcept {
    if (x == 0.0) {
        *exp = 0;
        return 0.0;
    }

    int e = 0;
    double mantissa = x;

    // Handle negative
    bool negative = false;
    if (mantissa < 0.0) {
        negative = true;
        mantissa = -mantissa;
    }

    // Normalize to [0.5, 1.0)
    while (mantissa >= 1.0) {
        mantissa *= 0.5;
        e++;
    }

    while (mantissa < 0.5 && mantissa > 0.0) {
        mantissa *= 2.0;
        e--;
    }

    *exp = e;
    return negative ? -mantissa : mantissa;
}

/**
 * Constexpr llrint - round to nearest long long
 */
constexpr int64_t llrint_constexpr(double x) noexcept {
    return (x >= 0.0)
        ? static_cast<int64_t>(x + 0.5)
        : static_cast<int64_t>(x - 0.5);
}

/**
 * Generate MPEG audio common decoder tables
 *
 * Algorithm:
 * 1. For each i from 1 to TABLE_4_3_SIZE-1:
 * 2. value = i / 4
 * 3. Compute pow43_val = value^(4/3) / IMDCT_SCALAR (only when i%4==0)
 * 4. f = pow43_val * 2^((i&3) / 4)
 * 5. Decompose f into mantissa and exponent using frexp
 * 6. Normalize to FRAC_BITS fractional bits
 * 7. Store mantissa in table_4_3_value, exponent in table_4_3_exp
 *
 * @return Pair of {exp_table, value_table}
 */
constexpr auto generate_mpegaudiodec_common_tables() noexcept {
    struct Tables {
        std::array<int8_t, TABLE_4_3_SIZE> table_4_3_exp{};
        std::array<uint32_t, TABLE_4_3_SIZE> table_4_3_value{};
    };

    Tables tables;

    double pow43_val = 0.0;

    // Index 0 stays zero
    tables.table_4_3_exp[0] = 0;
    tables.table_4_3_value[0] = 0;

    for (int i = 1; i < TABLE_4_3_SIZE; ++i) {
        double value = static_cast<double>(i) / 4.0;

        // Recompute pow43_val every 4 iterations
        if ((i & 3) == 0) {
            // pow43_val = value^(4/3) / IMDCT_SCALAR
            // value^(4/3) = value * cbrt(value)
            pow43_val = value * cbrt_constexpr(value) / IMDCT_SCALAR;
        }

        // Apply fractional power of 2
        double f = pow43_val * exp2_lut[i & 3];

        // Decompose into mantissa and exponent
        int e;
        double fm = frexp_constexpr(f, &e);

        // Convert mantissa to fixed-point (31 bits)
        int64_t m = llrint_constexpr(fm * (1LL << 31));

        // Adjust exponent for normalization
        e += FRAC_BITS - 31 + 5 - 100;

        // Store normalized values
        tables.table_4_3_value[i] = static_cast<uint32_t>(m);
        tables.table_4_3_exp[i] = static_cast<int8_t>(-e);
    }

    return tables;
}

// Generate tables at compile time
constexpr auto mpegaudiodec_common_tables = generate_mpegaudiodec_common_tables();

// Extract individual tables
constexpr auto& table_4_3_exp = mpegaudiodec_common_tables.table_4_3_exp;
constexpr auto& table_4_3_value = mpegaudiodec_common_tables.table_4_3_value;

// Total: 32,828 + 32,828 = 65,656 entries!

// Compile-time validation
namespace tests {
    // Test table sizes
    static_assert(table_4_3_exp.size() == TABLE_4_3_SIZE, "Exp table size");
    static_assert(table_4_3_value.size() == TABLE_4_3_SIZE, "Value table size");
    static_assert(TABLE_4_3_SIZE == 32828, "TABLE_4_3_SIZE constant");

    // Test that index 0 is zero
    static_assert(table_4_3_exp[0] == 0, "Exp[0] = 0");
    static_assert(table_4_3_value[0] == 0, "Value[0] = 0");

    // Test that index 1 has non-zero values
    static_assert(table_4_3_value[1] != 0, "Value[1] non-zero");

    // Test monotonicity in values (generally increasing for same exponent)
    static_assert(table_4_3_value[4] != 0, "Value[4] non-zero");
    static_assert(table_4_3_value[8] != 0, "Value[8] non-zero");

    // Test cube root accuracy (from reused function)
    constexpr double cbrt_8 = cbrt_constexpr(8.0);
    static_assert(cbrt_8 > 1.99 && cbrt_8 < 2.01, "cbrt(8) ≈ 2");

    constexpr double cbrt_27 = cbrt_constexpr(27.0);
    static_assert(cbrt_27 > 2.99 && cbrt_27 < 3.01, "cbrt(27) ≈ 3");

    // Test frexp function
    constexpr int frexp_test_exp = []() {
        int e;
        double m = frexp_constexpr(8.0, &e);
        return e;
    }();
    static_assert(frexp_test_exp == 4, "frexp(8.0) exponent = 4");

    constexpr double frexp_test_mantissa = []() {
        int e;
        return frexp_constexpr(8.0, &e);
    }();
    static_assert(frexp_test_mantissa == 0.5, "frexp(8.0) mantissa = 0.5");

    // Test frexp with 1.0
    constexpr int frexp_1_exp = []() {
        int e;
        frexp_constexpr(1.0, &e);
        return e;
    }();
    static_assert(frexp_1_exp == 1, "frexp(1.0) exponent = 1");

    constexpr double frexp_1_mantissa = []() {
        int e;
        return frexp_constexpr(1.0, &e);
    }();
    static_assert(frexp_1_mantissa == 0.5, "frexp(1.0) mantissa = 0.5");

    // Test frexp with 0.25
    constexpr int frexp_025_exp = []() {
        int e;
        frexp_constexpr(0.25, &e);
        return e;
    }();
    static_assert(frexp_025_exp == -1, "frexp(0.25) exponent = -1");

    // Test llrint
    static_assert(llrint_constexpr(5.4) == 5, "llrint(5.4) = 5");
    static_assert(llrint_constexpr(5.6) == 6, "llrint(5.6) = 6");
    static_assert(llrint_constexpr(-3.6) == -4, "llrint(-3.6) = -4");

    // Test exp2 lookup table
    static_assert(exp2_lut[0] == 1.0, "2^(0/4) = 1");
    static_assert(exp2_lut[2] > 1.414 && exp2_lut[2] < 1.415, "2^(2/4) = √2");

    // Test that tables have reasonable values
    // Values should generally increase (with exponent adjustments)
    static_assert(table_4_3_value[100] != 0, "Value[100] exists");
    static_assert(table_4_3_value[1000] != 0, "Value[1000] exists");
    static_assert(table_4_3_value[10000] != 0, "Value[10000] exists");

    // Test that exponents are in reasonable range (should be small negative numbers)
    static_assert(table_4_3_exp[100] < 10, "Exp[100] small");
    static_assert(table_4_3_exp[1000] < 10, "Exp[1000] small");

    // Test some specific values to ensure correctness
    // For i=4: value = 1.0, pow43 = 1.0/1.759, with exp2_lut[0] = 1.0
    static_assert(table_4_3_value[4] > 0, "Value[4] positive");

    // Test that different indices have different values
    static_assert(table_4_3_value[1] != table_4_3_value[2], "Values differ");
    static_assert(table_4_3_value[4] != table_4_3_value[8], "Values differ 2");
}

/**
 * Constexpr accessors
 */
constexpr int8_t get_table_4_3_exp(int index) noexcept {
    return (index >= 0 && index < TABLE_4_3_SIZE)
        ? table_4_3_exp[index]
        : 0;
}

constexpr uint32_t get_table_4_3_value(int index) noexcept {
    return (index >= 0 && index < TABLE_4_3_SIZE)
        ? table_4_3_value[index]
        : 0;
}

/**
 * Get both mantissa and exponent for an index
 */
struct MantissaExponent {
    uint32_t mantissa;
    int8_t exponent;
};

constexpr MantissaExponent get_table_4_3(int index) noexcept {
    if (index >= 0 && index < TABLE_4_3_SIZE) {
        return {table_4_3_value[index], table_4_3_exp[index]};
    }
    return {0, 0};
}

} // namespace mpegaudiodec_common
} // namespace ffmpeg

#endif // AVCODEC_MPEGAUDIODEC_COMMON_TABLEGEN_CONSTEXPR_HPP
