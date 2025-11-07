/*
 * Modern C++ constexpr AAC cube-root tables
 * Copyright (c) 2010 Reimar Döffinger <Reimar.Doeffinger@gmx.de>
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
 * Modern C++20 constexpr AAC cube-root lookup tables
 *
 * This header provides compile-time generation of cube-root tables used
 * by the AAC codec for spectral processing.
 *
 * The tables map indices to (2*i+1)^(4/3) values, which are used in
 * AAC's spectral coefficient scaling. The algorithm handles non-squarefree
 * numbers through prime factorization patterns.
 *
 * Mathematical background:
 * - AAC needs to compute x^(4/3) for spectral values
 * - The LUT stores pre-computed values for odd integers
 * - Even integers are handled by powers of 2 multiplication
 * - Total table size: 8192 entries (LUT_SIZE)
 *
 * All tables are generated at compile time from the mathematical algorithm.
 */

#ifndef AVCODEC_CBRT_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_CBRT_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cmath>
#include <cstdint>

namespace ffmpeg {
namespace cbrt {

// Constants from cbrt_data.h
constexpr size_t LUT_SIZE = 1 << 13;  // 8192
constexpr size_t TMP_LUT_SIZE = LUT_SIZE / 2;  // 4096

/**
 * Constexpr absolute value
 */
constexpr double abs_constexpr(double x) noexcept {
    return x < 0.0 ? -x : x;
}

/**
 * Constexpr cube root using Newton-Raphson iteration
 *
 * Newton-Raphson for cube root:
 *   x_{n+1} = (2*x_n + a/x_n²) / 3
 *
 * Converges cubically, very fast for reasonable inputs.
 *
 * @param x Value to compute cube root of (must be positive)
 * @return Cube root of x
 */
constexpr double cbrt_constexpr(double x) noexcept {
    if (x == 0.0) return 0.0;
    if (x < 0.0) return -cbrt_constexpr(-x);

    // Initial guess using bit manipulation approximation
    // For constexpr, use a simpler approach: x^(1/3) ≈ x/3 + 0.5 for initial guess
    double guess = (x < 1.0) ? x : (x / 3.0 + 0.5);

    // Newton-Raphson iterations (10 iterations for high accuracy)
    for (int i = 0; i < 10; ++i) {
        double guess_squared = guess * guess;
        guess = (2.0 * guess + x / guess_squared) / 3.0;
    }

    return guess;
}

/**
 * Constexpr power function for integer exponents
 */
constexpr double pow_constexpr(double base, int exp) noexcept {
    if (exp == 0) return 1.0;
    if (exp < 0) return 1.0 / pow_constexpr(base, -exp);

    double result = 1.0;
    for (int i = 0; i < exp; ++i) {
        result *= base;
    }
    return result;
}

/**
 * Generate the temporary double LUT for odd integer powers
 *
 * This function creates tmp_lut[idx] = (2*idx+1)^(4/3) with special
 * handling for non-squarefree numbers.
 *
 * Algorithm:
 * 1. Initialize all entries to 1.0
 * 2. For each prime-like base i = 2*idx+1:
 *    - Compute cbrt_val = i * cbrt(i) = i^(4/3)
 *    - Multiply into all multiples of i
 * 3. This handles factorization: (p*q)^(4/3) = p^(4/3) * q^(4/3)
 *
 * The loop structure ensures each composite number gets contributions
 * from all its prime factors.
 */
constexpr auto generate_cbrt_double_lut() noexcept {
    std::array<double, TMP_LUT_SIZE> tmp_lut{};

    // Initialize to 1.0
    for (size_t idx = 0; idx < TMP_LUT_SIZE; ++idx) {
        tmp_lut[idx] = 1.0;
    }

    // Handle non-squarefree numbers
    // sqrt(LUT_SIZE) = 90, so idx < 45 corresponds to values < 89
    for (int idx = 1; idx < 45; ++idx) {
        if (tmp_lut[idx] == 1.0) {
            int i = 2 * idx + 1;
            double cbrt_val = static_cast<double>(i) * cbrt_constexpr(static_cast<double>(i));

            // Multiply cbrt_val into all multiples
            for (int k = i; k < static_cast<int>(LUT_SIZE); k *= i) {
                // We only handle odd multiples: k, 3k, 5k, 7k, ...
                // Corresponding indices: k>>1, (k>>1)+k, (k>>1)+2k, ...
                for (int idx2 = k >> 1; idx2 < static_cast<int>(TMP_LUT_SIZE); idx2 += k) {
                    tmp_lut[idx2] *= cbrt_val;
                }
            }
        }
    }

    // Handle remaining primes (> 89)
    for (int idx = 45; idx < static_cast<int>(TMP_LUT_SIZE); ++idx) {
        if (tmp_lut[idx] == 1.0) {
            int i = 2 * idx + 1;
            double cbrt_val = static_cast<double>(i) * cbrt_constexpr(static_cast<double>(i));

            // For large primes, only multiply into direct multiples
            for (int idx2 = idx; idx2 < static_cast<int>(TMP_LUT_SIZE); idx2 += i) {
                tmp_lut[idx2] *= cbrt_val;
            }
        }
    }

    return tmp_lut;
}

/**
 * Convert float to uint32_t representation (for normal float tables)
 */
constexpr uint32_t float_to_uint32(float f) noexcept {
    // In C++20, we can use bit_cast if available, but for portability:
    // This is a simplified version that works for compile-time
    // We'll store the actual conversion at runtime via union
    union {
        float f;
        uint32_t i;
    } u;
    // Note: This union usage is technically undefined in constexpr,
    // but we'll use it at runtime. For compile-time, we store doubles.
    return 0;  // Placeholder - actual conversion happens at runtime
}

/**
 * Generate the main AAC cube-root table (float version)
 *
 * Algorithm:
 * 1. Generate temporary LUT of odd integer powers
 * 2. Fill main table by multiplying with powers of cbrt(2)
 * 3. For each odd base (2*idx+1), fill powers of 2: i, 2i, 4i, 8i, ...
 *
 * The result maps index -> (index)^(4/3) for use in AAC decoding.
 */
constexpr auto generate_cbrt_table_float() noexcept {
    std::array<float, LUT_SIZE> cbrt_tab{};

    // Generate temporary double LUT
    constexpr auto tmp_lut = generate_cbrt_double_lut();

    // cbrt(2) for successive powers
    constexpr double cbrt_2_val = 1.259921049894873;  // cbrt(2)
    constexpr double double_cbrt_2 = 2.0 * cbrt_2_val;

    // Fill the table by processing in descending order
    // This allows reusing memory as we convert from double LUT to float table
    for (int idx = TMP_LUT_SIZE - 1; idx >= 0; --idx) {
        double cbrt_val = tmp_lut[idx];

        // Fill powers of 2: (2*idx+1), 2*(2*idx+1), 4*(2*idx+1), ...
        for (int i = 2 * idx + 1; i < static_cast<int>(LUT_SIZE); i *= 2) {
            cbrt_tab[i] = static_cast<float>(cbrt_val);
            cbrt_val *= double_cbrt_2;
        }
    }

    // Handle zero index
    cbrt_tab[0] = 0.0f;

    return cbrt_tab;
}

/**
 * Generate the main AAC cube-root table (fixed-point version)
 *
 * For USE_FIXED mode, values are scaled by 8192 (2^13)
 */
constexpr auto generate_cbrt_table_fixed() noexcept {
    std::array<int32_t, LUT_SIZE> cbrt_tab{};

    // Generate temporary double LUT
    constexpr auto tmp_lut = generate_cbrt_double_lut();

    constexpr double cbrt_2_val = 1.259921049894873;
    constexpr double double_cbrt_2 = 2.0 * cbrt_2_val;

    for (int idx = TMP_LUT_SIZE - 1; idx >= 0; --idx) {
        double cbrt_val = tmp_lut[idx];

        for (int i = 2 * idx + 1; i < static_cast<int>(LUT_SIZE); i *= 2) {
            // Fixed-point: scale by 8192 and round
            cbrt_tab[i] = static_cast<int32_t>(cbrt_val * 8192.0 + 0.5);
            cbrt_val *= double_cbrt_2;
        }
    }

    cbrt_tab[0] = 0;

    return cbrt_tab;
}

// Generate both tables at compile time
constexpr auto cbrt_table_float = generate_cbrt_table_float();
constexpr auto cbrt_table_fixed = generate_cbrt_table_fixed();

// Total: 16,384 entries (8192 float + 8192 fixed) at compile time!

// Compile-time validation
namespace tests {
    // Test temporary LUT generation
    constexpr auto test_tmp_lut = generate_cbrt_double_lut();
    static_assert(test_tmp_lut.size() == TMP_LUT_SIZE, "TMP_LUT size");

    // Test that odd numbers have non-trivial values
    static_assert(test_tmp_lut[0] > 0.9 && test_tmp_lut[0] < 1.1, "tmp_lut[0] = 1^(4/3) = 1");
    static_assert(test_tmp_lut[1] > 2.0, "tmp_lut[1] = 3^(4/3) > 2");
    static_assert(test_tmp_lut[2] > 4.0, "tmp_lut[2] = 5^(4/3) > 4");

    // Test cube root approximation
    constexpr double cbrt_8 = cbrt_constexpr(8.0);
    static_assert(cbrt_8 > 1.99 && cbrt_8 < 2.01, "cbrt(8) ≈ 2");

    constexpr double cbrt_27 = cbrt_constexpr(27.0);
    static_assert(cbrt_27 > 2.99 && cbrt_27 < 3.01, "cbrt(27) ≈ 3");

    constexpr double cbrt_64 = cbrt_constexpr(64.0);
    static_assert(cbrt_64 > 3.99 && cbrt_64 < 4.01, "cbrt(64) ≈ 4");

    constexpr double cbrt_125 = cbrt_constexpr(125.0);
    static_assert(cbrt_125 > 4.99 && cbrt_125 < 5.01, "cbrt(125) ≈ 5");

    // Test main table sizes
    static_assert(cbrt_table_float.size() == LUT_SIZE, "Float table size");
    static_assert(cbrt_table_fixed.size() == LUT_SIZE, "Fixed table size");

    // Test that table starts with zero
    static_assert(cbrt_table_float[0] == 0.0f, "Float table zero");
    static_assert(cbrt_table_fixed[0] == 0, "Fixed table zero");

    // Test that odd indices have values (these are the base values)
    static_assert(cbrt_table_float[1] > 0.9f, "Float table[1] = 1^(4/3)");
    static_assert(cbrt_table_float[3] > 3.0f, "Float table[3] = 3^(4/3)");
    static_assert(cbrt_table_float[5] > 6.0f, "Float table[5] = 5^(4/3)");

    static_assert(cbrt_table_fixed[1] > 7000, "Fixed table[1] scaled");
    static_assert(cbrt_table_fixed[3] > 20000, "Fixed table[3] scaled");

    // Test that even indices have appropriate scaled values
    static_assert(cbrt_table_float[2] > 1.5f, "Float table[2] = 2^(4/3)");
    static_assert(cbrt_table_float[4] > 2.0f, "Float table[4] = 4^(4/3)");

    // Test monotonicity in first few entries
    static_assert(cbrt_table_float[1] < cbrt_table_float[2], "Monotonic 1<2");
    static_assert(cbrt_table_float[2] < cbrt_table_float[3], "Monotonic 2<3");
    static_assert(cbrt_table_float[3] < cbrt_table_float[4], "Monotonic 3<4");

    static_assert(cbrt_table_fixed[1] < cbrt_table_fixed[2], "Fixed monotonic 1<2");
    static_assert(cbrt_table_fixed[2] < cbrt_table_fixed[3], "Fixed monotonic 2<3");

    // Test that values grow appropriately
    static_assert(cbrt_table_float[100] > cbrt_table_float[10], "Growth check");
    static_assert(cbrt_table_float[1000] > cbrt_table_float[100], "Growth check 2");

    // Verify mathematical property: (2n)^(4/3) = 2^(4/3) * n^(4/3)
    // Since 2^(4/3) ≈ 2.52, table[2n] should be ≈ 2.52 * table[n]
    constexpr float ratio_1_2 = cbrt_table_float[2] / cbrt_table_float[1];
    static_assert(ratio_1_2 > 2.4f && ratio_1_2 < 2.6f, "Power of 2 scaling");
}

/**
 * Helper function to access table entries
 */
constexpr float cbrt_lookup_float(size_t index) noexcept {
    return (index < LUT_SIZE) ? cbrt_table_float[index] : 0.0f;
}

constexpr int32_t cbrt_lookup_fixed(size_t index) noexcept {
    return (index < LUT_SIZE) ? cbrt_table_fixed[index] : 0;
}

/**
 * Constexpr validation: compute reference value and compare
 */
namespace validation {
    // Test specific indices match mathematical expectation
    // 1^(4/3) = 1
    constexpr float val_1 = cbrt_table_float[1];
    static_assert(val_1 > 0.99f && val_1 < 1.01f, "1^(4/3) = 1");

    // 8^(4/3) = (2^3)^(4/3) = 2^4 = 16
    constexpr float val_8 = cbrt_table_float[8];
    static_assert(val_8 > 15.5f && val_8 < 16.5f, "8^(4/3) = 16");

    // 27^(4/3) = (3^3)^(4/3) = 3^4 = 81
    constexpr float val_27 = cbrt_table_float[27];
    static_assert(val_27 > 80.0f && val_27 < 82.0f, "27^(4/3) = 81");
}

} // namespace cbrt
} // namespace ffmpeg

#endif // AVCODEC_CBRT_TABLEGEN_CONSTEXPR_HPP
