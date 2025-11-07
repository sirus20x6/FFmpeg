/*
 * Modern C++ constexpr MPEG Audio (MP3) codec tables
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
 * Modern C++20 constexpr MPEG Audio (MP3) decoder tables
 *
 * This header provides compile-time generation of MP3 decoder dequantization
 * tables. These tables are used in the MPEG-1/2 Layer III (MP3) audio decoder
 * for converting quantized spectral coefficients back to floating-point values.
 *
 * Tables generated:
 * - exp_table: Exponent scaling factors [512 entries]
 * - expval_table: Value * exponent combinations [512x16 = 8,192 entries]
 * - Both float and fixed-point (uint32_t) variants
 *
 * Total: 17,408 entries (8,704 float + 8,704 fixed-point)
 *
 * Mathematical background:
 * - Computes value^(4/3) * 2^(exponent) / IMDCT_SCALAR
 * - Uses lookup tables for 2^(n/4) to avoid expensive pow() calls
 * - Pre-computes all combinations for fast decoding
 *
 * All tables generated at compile time from the MP3 specification formulas.
 */

#ifndef AVCODEC_MPEGAUDIO_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_MPEGAUDIO_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>
#include <cmath>

namespace ffmpeg {
namespace mpegaudio {

// Constants from MP3 specification
constexpr int EXP_TABLE_SIZE = 512;
constexpr int VALUE_TABLE_SIZE = 16;
constexpr double IMDCT_SCALAR = 1.759;

// Use cube root from cbrt_tablegen_constexpr.hpp
// (Replicate here for self-contained header)

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

/**
 * Constexpr power function for integer exponents
 */
constexpr double pow_int_constexpr(double base, int exp) noexcept {
    if (exp == 0) return 1.0;
    if (exp < 0) return 1.0 / pow_int_constexpr(base, -exp);

    double result = 1.0;
    for (int i = 0; i < exp; ++i) {
        result *= base;
    }
    return result;
}

/**
 * Constexpr exp2 for fractional powers (specifically 2^(n/4))
 *
 * Pre-computed lookup table for 2^(0/4), 2^(1/4), 2^(2/4), 2^(3/4)
 */
constexpr std::array<double, 4> exp2_lut = {
    1.00000000000000000000,  // 2^(0/4) = 1.0
    1.18920711500272106672,  // 2^(1/4) ≈ 1.189207
    1.41421356237309504880,  // 2^(2/4) = √2 ≈ 1.414214
    1.68179283050742908606,  // 2^(3/4) ≈ 1.681793
};

/**
 * Generate pow43 lookup table
 *
 * Computes value^(4/3) = value * cbrt(value) for values 0-15
 *
 * This is used for MP3 spectral coefficient dequantization.
 *
 * @return Array of 16 double values
 */
constexpr auto generate_pow43_lut() noexcept {
    std::array<double, 16> lut{};

    for (int i = 0; i < 16; ++i) {
        double val = static_cast<double>(i);
        // i^(4/3) = i * i^(1/3) = i * cbrt(i)
        lut[i] = val * cbrt_constexpr(val);
    }

    return lut;
}

constexpr auto pow43_lut = generate_pow43_lut();

/**
 * Constexpr llrint (long long round to nearest integer)
 */
constexpr int64_t llrint_constexpr(double x) noexcept {
    return (x >= 0.0)
        ? static_cast<int64_t>(x + 0.5)
        : static_cast<int64_t>(x - 0.5);
}

/**
 * Generate MPEG audio exponent/value tables (float version)
 *
 * Formula for each [exponent][value]:
 *   result = value^(4/3) * 2^((exponent - 72*4) / 4) / IMDCT_SCALAR
 *
 * The exponent represents powers of 2^(1/4), with base at 2^(-72).
 * This allows efficient computation of the dequantization formula
 * without expensive floating-point operations during decoding.
 *
 * @return Pair of {exp_table[512], expval_table[512][16]}
 */
constexpr auto generate_mpegaudio_tables_float() noexcept {
    struct Tables {
        std::array<float, EXP_TABLE_SIZE> exp_table{};
        std::array<std::array<float, VALUE_TABLE_SIZE>, EXP_TABLE_SIZE> expval_table{};
    };

    Tables tables;

    // Base: 2^(-72) = 2^(-288/4)
    // This is a very small number: ~2.1176e-22
    constexpr double exp2_base_initial = 2.11758236813575084767080625169910490512847900390625e-22;

    double exp2_base = exp2_base_initial;

    for (int exponent = 0; exponent < EXP_TABLE_SIZE; ++exponent) {
        // Every 4 exponents, multiply base by 2
        if (exponent != 0 && (exponent & 3) == 0) {
            exp2_base *= 2.0;
        }

        // Get fractional part using lookup table
        double exp2_val = exp2_base * exp2_lut[exponent & 3] / IMDCT_SCALAR;

        // Compute all value combinations
        for (int value = 0; value < VALUE_TABLE_SIZE; ++value) {
            double f = pow43_lut[value] * exp2_val;
            tables.expval_table[exponent][value] = static_cast<float>(f);
        }

        // exp_table is just expval_table[exponent][1]
        tables.exp_table[exponent] = tables.expval_table[exponent][1];
    }

    return tables;
}

/**
 * Generate MPEG audio exponent/value tables (fixed-point version)
 *
 * Same as float version but stores as uint32_t for fixed-point decoding.
 * Values are rounded to nearest integer and clamped at 0xFFFFFFFF.
 *
 * @return Pair of {exp_table[512], expval_table[512][16]}
 */
constexpr auto generate_mpegaudio_tables_fixed() noexcept {
    struct Tables {
        std::array<uint32_t, EXP_TABLE_SIZE> exp_table{};
        std::array<std::array<uint32_t, VALUE_TABLE_SIZE>, EXP_TABLE_SIZE> expval_table{};
    };

    Tables tables;

    constexpr double exp2_base_initial = 2.11758236813575084767080625169910490512847900390625e-22;
    double exp2_base = exp2_base_initial;

    for (int exponent = 0; exponent < EXP_TABLE_SIZE; ++exponent) {
        if (exponent != 0 && (exponent & 3) == 0) {
            exp2_base *= 2.0;
        }

        double exp2_val = exp2_base * exp2_lut[exponent & 3] / IMDCT_SCALAR;

        for (int value = 0; value < VALUE_TABLE_SIZE; ++value) {
            double f = pow43_lut[value] * exp2_val;

            // Clamp to uint32_t max
            if (f >= 4294967295.0) {  // 0xFFFFFFFF
                tables.expval_table[exponent][value] = 0xFFFFFFFF;
            } else {
                tables.expval_table[exponent][value] =
                    static_cast<uint32_t>(llrint_constexpr(f));
            }
        }

        tables.exp_table[exponent] = tables.expval_table[exponent][1];
    }

    return tables;
}

// Generate both float and fixed tables at compile time
constexpr auto mpegaudio_tables_float = generate_mpegaudio_tables_float();
constexpr auto mpegaudio_tables_fixed = generate_mpegaudio_tables_fixed();

// Extract individual tables for easier access
constexpr auto& exp_table_float = mpegaudio_tables_float.exp_table;
constexpr auto& expval_table_float = mpegaudio_tables_float.expval_table;
constexpr auto& exp_table_fixed = mpegaudio_tables_fixed.exp_table;
constexpr auto& expval_table_fixed = mpegaudio_tables_fixed.expval_table;

// Total: 512 + 8,192 + 512 + 8,192 = 17,408 entries!

// Compile-time validation
namespace tests {
    // Test table sizes
    static_assert(exp_table_float.size() == EXP_TABLE_SIZE, "Float exp table size");
    static_assert(expval_table_float.size() == EXP_TABLE_SIZE, "Float expval table size");
    static_assert(expval_table_float[0].size() == VALUE_TABLE_SIZE, "Expval row size");

    static_assert(exp_table_fixed.size() == EXP_TABLE_SIZE, "Fixed exp table size");
    static_assert(expval_table_fixed.size() == EXP_TABLE_SIZE, "Fixed expval table size");

    // Test pow43 lookup table
    static_assert(pow43_lut.size() == 16, "Pow43 LUT size");
    static_assert(pow43_lut[0] == 0.0, "0^(4/3) = 0");
    static_assert(pow43_lut[1] == 1.0, "1^(4/3) = 1");

    // Test 8^(4/3) = (2^3)^(4/3) = 2^4 = 16
    constexpr double pow43_8 = pow43_lut[8];
    static_assert(pow43_8 > 15.5 && pow43_8 < 16.5, "8^(4/3) = 16");

    // Test cube root accuracy
    constexpr double cbrt_8 = cbrt_constexpr(8.0);
    static_assert(cbrt_8 > 1.99 && cbrt_8 < 2.01, "cbrt(8) ≈ 2");

    constexpr double cbrt_27 = cbrt_constexpr(27.0);
    static_assert(cbrt_27 > 2.99 && cbrt_27 < 3.01, "cbrt(27) ≈ 3");

    // Test exp2 lookup table values
    static_assert(exp2_lut[0] == 1.0, "2^(0/4) = 1");
    static_assert(exp2_lut[2] > 1.414 && exp2_lut[2] < 1.415, "2^(2/4) = √2");

    // Test that exp_table is expval_table[exp][1]
    static_assert(exp_table_float[0] == expval_table_float[0][1], "Exp table relation");
    static_assert(exp_table_float[100] == expval_table_float[100][1], "Exp table relation 2");

    static_assert(exp_table_fixed[0] == expval_table_fixed[0][1], "Fixed exp table relation");

    // Test that expval_table[exp][0] is always 0 (since 0^(4/3) = 0)
    static_assert(expval_table_float[0][0] == 0.0f, "expval[exp][0] = 0");
    static_assert(expval_table_float[100][0] == 0.0f, "expval[exp][0] = 0");
    static_assert(expval_table_float[500][0] == 0.0f, "expval[exp][0] = 0");

    static_assert(expval_table_fixed[0][0] == 0, "Fixed expval[exp][0] = 0");
    static_assert(expval_table_fixed[100][0] == 0, "Fixed expval[exp][0] = 0");

    // Test monotonicity: higher exponents give larger values (for same value > 0)
    static_assert(expval_table_float[0][1] < expval_table_float[100][1],
                  "Monotonic in exponent");
    static_assert(expval_table_float[100][1] < expval_table_float[200][1],
                  "Monotonic in exponent 2");

    // Test monotonicity: higher values give larger results (for same exponent)
    static_assert(expval_table_float[100][1] < expval_table_float[100][5],
                  "Monotonic in value");
    static_assert(expval_table_float[100][5] < expval_table_float[100][10],
                  "Monotonic in value 2");

    // Test that fixed-point values are reasonable integers
    static_assert(expval_table_fixed[0][1] >= 0, "Fixed values non-negative");
    static_assert(expval_table_fixed[100][5] > 0, "Fixed values positive");

    // Test that early exponents produce very small values (near zero)
    static_assert(expval_table_float[0][1] < 1.0e-20f, "Early exponent very small");
    static_assert(expval_table_float[10][1] < 1.0e-19f, "Early exponent small");

    // Test that later exponents produce larger values
    static_assert(expval_table_float[400][15] > 0.001f, "Late exponent larger");

    // Test llrint function
    static_assert(llrint_constexpr(5.4) == 5, "llrint rounds down");
    static_assert(llrint_constexpr(5.6) == 6, "llrint rounds up");
    static_assert(llrint_constexpr(-3.6) == -4, "llrint negative");

    // Test power function
    constexpr double pow_2_3 = pow_int_constexpr(2.0, 3);
    static_assert(pow_2_3 > 7.9 && pow_2_3 < 8.1, "2^3 = 8");

    constexpr double pow_2_10 = pow_int_constexpr(2.0, 10);
    static_assert(pow_2_10 > 1023 && pow_2_10 < 1025, "2^10 = 1024");
}

/**
 * Constexpr accessors for table data
 */
constexpr float exp_table_float_lookup(int exponent) noexcept {
    return (exponent >= 0 && exponent < EXP_TABLE_SIZE)
        ? exp_table_float[exponent]
        : 0.0f;
}

constexpr float expval_table_float_lookup(int exponent, int value) noexcept {
    return (exponent >= 0 && exponent < EXP_TABLE_SIZE &&
            value >= 0 && value < VALUE_TABLE_SIZE)
        ? expval_table_float[exponent][value]
        : 0.0f;
}

constexpr uint32_t exp_table_fixed_lookup(int exponent) noexcept {
    return (exponent >= 0 && exponent < EXP_TABLE_SIZE)
        ? exp_table_fixed[exponent]
        : 0;
}

constexpr uint32_t expval_table_fixed_lookup(int exponent, int value) noexcept {
    return (exponent >= 0 && exponent < EXP_TABLE_SIZE &&
            value >= 0 && value < VALUE_TABLE_SIZE)
        ? expval_table_fixed[exponent][value]
        : 0;
}

} // namespace mpegaudio
} // namespace ffmpeg

#endif // AVCODEC_MPEGAUDIO_TABLEGEN_CONSTEXPR_HPP
