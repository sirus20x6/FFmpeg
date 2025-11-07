/*
 * Modern C++ constexpr AAC power tables
 * Copyright (c) 2005-2006 Oded Shimon (ods15@ods15.dyndns.org)
 * Copyright (c) 2006-2007 Maxim Gavrilov (maxim.gavrilov@gmail.com)
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
 * Modern C++20 constexpr AAC power tables
 *
 * AAC (Advanced Audio Coding) uses quantized spectral coefficients that require
 * efficient dequantization via power functions. These tables provide pre-computed
 * power values for scale factor operations.
 *
 * Tables:
 * - ff_aac_pow2sf_tab[428]: Computes 2^((i - 200) / 4) for i ∈ [0, 428)
 * - ff_aac_pow34sf_tab[428]: Computes 2^(3*(i - 200) / 16) for i ∈ [0, 428)
 *
 * The second table is the first raised to the 3/4 power:
 * pow34sf[i] = (pow2sf[i])^(3/4) = (2^((i-200)/4))^(3/4) = 2^(3*(i-200)/16)
 *
 * Algorithm (from aactab.c):
 * Uses clever decomposition to avoid expensive pow() calls:
 * - Precompute 2^(i/16) for i ∈ [0, 15] (fractional parts)
 * - Track whole-number part separately with periodic doubling
 * - Combine: value = whole_part × exp2_lut[fractional_index]
 *
 * For pow2sf: starts with 2^(-50), doubles every 4 steps
 * For pow34sf: starts with 2^(-38), doubles with different period
 *
 * Total: 856 float entries (428 × 2 tables)
 *
 * Used by: AAC decoder/encoder for scale factor dequantization
 */

#ifndef AVCODEC_AAC_POW_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_AAC_POW_TABLEGEN_CONSTEXPR_HPP

#include <array>

namespace ffmpeg {
namespace aac {

// Constants
constexpr int TABLE_SIZE = 428;
constexpr int POW_SF2_ZERO = 200;  // Index corresponding to 2^0 = 1

/**
 * Lookup table for 2^(i/16) where i ∈ [0, 15]
 *
 * This provides fractional powers of 2 with 1/16 granularity.
 * Used as building blocks for generating the main tables.
 */
constexpr std::array<float, 16> exp2_lut = {
    1.00000000000000000000f,  // 2^(0/16)
    1.04427378242741384032f,  // 2^(1/16)
    1.09050773266525765921f,  // 2^(2/16)
    1.13878863475669165370f,  // 2^(3/16)
    1.18920711500272106672f,  // 2^(4/16) = 2^(1/4)
    1.24185781207348404859f,  // 2^(5/16)
    1.29683955465100966593f,  // 2^(6/16)
    1.35425554693689272830f,  // 2^(7/16)
    1.41421356237309504880f,  // 2^(8/16) = 2^(1/2) = √2
    1.47682614593949931139f,  // 2^(9/16)
    1.54221082540794082361f,  // 2^(10/16)
    1.61049033194925430818f,  // 2^(11/16)
    1.68179283050742908606f,  // 2^(12/16) = 2^(3/4)
    1.75625216037329948311f,  // 2^(13/16)
    1.83400808640934246349f,  // 2^(14/16)
    1.91520656139714729387f,  // 2^(15/16)
};

/**
 * Generate ff_aac_pow2sf_tab at compile time
 *
 * Computes 2^((i - POW_SF2_ZERO) / 4) = 2^((i - 200) / 4)
 *
 * Algorithm (from aactab.c lines 76-90):
 * - Start with t1 = 2^(-50) = 2^((0 - 200) / 4)
 * - t1_inc = 4 * (i % 4) gives indices: 0, 4, 8, 12, 0, 4, ...
 * - When t1_inc wraps (goes from 12 to 0), double t1
 * - Result = t1 * exp2_lut[t1_inc]
 *
 * Explanation:
 * (i - 200) / 4 = floor((i - 200) / 4) + (i % 4) / 4
 *
 * t1 tracks 2^(floor((i - 200) / 4))
 * exp2_lut[4 * (i % 4)] gives 2^((i % 4) / 4)
 *
 * So: t1 * exp2_lut[4 * (i % 4)] = 2^((i - 200) / 4) ✓
 */
constexpr auto generate_aac_pow2sf_tab() noexcept {
    std::array<float, TABLE_SIZE> table{};

    // Start: i = 0, (0 - 200) / 4 = -50, so t1 = 2^(-50)
    float t1 = 8.8817841970012523233890533447265625e-16f;  // 2^(-50)
    int t1_inc_prev = 0;

    for (int i = 0; i < TABLE_SIZE; ++i) {
        // Fractional part index: cycles through 0, 4, 8, 12
        int t1_inc_cur = 4 * (i % 4);

        // When we wrap around (12 → 0), we've advanced by 1 in the whole part
        if (t1_inc_cur < t1_inc_prev) {
            t1 *= 2.0f;
        }

        // Combine whole and fractional parts
        table[i] = t1 * exp2_lut[t1_inc_cur];

        t1_inc_prev = t1_inc_cur;
    }

    return table;
}

/**
 * Generate ff_aac_pow34sf_tab at compile time
 *
 * Computes 2^(3*(i - POW_SF2_ZERO) / 16) = 2^(3*(i - 200) / 16)
 *
 * This is equivalent to (pow2sf[i])^(3/4) since:
 * (2^((i-200)/4))^(3/4) = 2^(3*(i-200)/16)
 *
 * Algorithm (from aactab.c lines 76-90):
 * - Start with t2 = 2^(-38) ≈ 2^(3*(0 - 200) / 16)
 * - Actually: 3 * (-200) / 16 = -37.5, but we start at -38 for rounding
 * - t2_inc = (8 + 3*i) % 16 gives fractional indices
 * - When t2_inc wraps (15 → lower), double t2
 * - Result = t2 * exp2_lut[t2_inc]
 *
 * Explanation:
 * The index (8 + 3*i) % 16 creates a pattern that cycles through
 * 16 values in a non-sequential order, with periodic doubling of t2
 * to track the whole-number part of 3*(i-200)/16.
 */
constexpr auto generate_aac_pow34sf_tab() noexcept {
    std::array<float, TABLE_SIZE> table{};

    // Start: i = 0, 3*(0 - 200) / 16 ≈ -37.5, so t2 ≈ 2^(-38)
    float t2 = 3.63797880709171295166015625e-12f;  // 2^(-38)
    int t2_inc_prev = 8;

    for (int i = 0; i < TABLE_SIZE; ++i) {
        // Fractional part index: (8 + 3*i) % 16
        int t2_inc_cur = (8 + 3 * i) % 16;

        // When index wraps to a smaller value, advance whole part
        if (t2_inc_cur < t2_inc_prev) {
            t2 *= 2.0f;
        }

        // Combine whole and fractional parts
        table[i] = t2 * exp2_lut[t2_inc_cur];

        t2_inc_prev = t2_inc_cur;
    }

    return table;
}

// Generate tables at compile time
constexpr auto aac_pow2sf_tab = generate_aac_pow2sf_tab();
constexpr auto aac_pow34sf_tab = generate_aac_pow34sf_tab();

// Total: 428 + 428 = 856 float entries!

// Compile-time validation
namespace tests {
    // Test table sizes
    static_assert(aac_pow2sf_tab.size() == TABLE_SIZE, "pow2sf size = 428");
    static_assert(aac_pow34sf_tab.size() == TABLE_SIZE, "pow34sf size = 428");

    // Test constants
    static_assert(TABLE_SIZE == 428, "Table size constant");
    static_assert(POW_SF2_ZERO == 200, "Zero index constant");

    // Test exp2_lut
    static_assert(exp2_lut.size() == 16, "exp2_lut size = 16");
    static_assert(exp2_lut[0] >= 0.99f && exp2_lut[0] <= 1.01f,
                  "exp2_lut[0] = 1 (2^0)");
    static_assert(exp2_lut[8] >= 1.41f && exp2_lut[8] <= 1.42f,
                  "exp2_lut[8] = √2 (2^(1/2))");

    // Test pow2sf_tab key values
    // Index 200 should give 2^((200-200)/4) = 2^0 = 1
    static_assert(aac_pow2sf_tab[200] >= 0.99f && aac_pow2sf_tab[200] <= 1.01f,
                  "pow2sf[200] ≈ 1 (2^0)");

    // Index 204 should give 2^((204-200)/4) = 2^1 = 2
    static_assert(aac_pow2sf_tab[204] >= 1.99f && aac_pow2sf_tab[204] <= 2.01f,
                  "pow2sf[204] ≈ 2 (2^1)");

    // Index 0 should give 2^((0-200)/4) = 2^(-50) (very small)
    static_assert(aac_pow2sf_tab[0] > 0.0f && aac_pow2sf_tab[0] < 1e-14f,
                  "pow2sf[0] ≈ 2^(-50) (very small)");

    // Index 427 should give 2^((427-200)/4) ≈ 2^56.75 (very large)
    static_assert(aac_pow2sf_tab[427] > 1e16f,
                  "pow2sf[427] ≈ 2^56.75 (very large)");

    // Test pow34sf_tab key values
    // Index 200: 2^(3*(200-200)/16) = 2^0 = 1
    static_assert(aac_pow34sf_tab[200] >= 0.99f && aac_pow34sf_tab[200] <= 1.01f,
                  "pow34sf[200] ≈ 1 (2^0)");

    // Test relationship: pow34sf[i] ≈ (pow2sf[i])^(3/4)
    // For index 204: pow2sf = 2, so pow34sf should be 2^(3/4) ≈ 1.68
    static_assert(aac_pow34sf_tab[204] >= 1.67f && aac_pow34sf_tab[204] <= 1.69f,
                  "pow34sf[204] ≈ 2^(3/4)");

    // Test monotonicity (both tables should be strictly increasing)
    static_assert(aac_pow2sf_tab[1] > aac_pow2sf_tab[0], "pow2sf monotonic start");
    static_assert(aac_pow2sf_tab[200] > aac_pow2sf_tab[199], "pow2sf monotonic middle");
    static_assert(aac_pow2sf_tab[427] > aac_pow2sf_tab[426], "pow2sf monotonic end");

    static_assert(aac_pow34sf_tab[1] > aac_pow34sf_tab[0], "pow34sf monotonic start");
    static_assert(aac_pow34sf_tab[200] > aac_pow34sf_tab[199], "pow34sf monotonic middle");
    static_assert(aac_pow34sf_tab[427] > aac_pow34sf_tab[426], "pow34sf monotonic end");

    // Test that pow34sf < pow2sf for indices > 200 (since x^(3/4) < x when x > 1)
    static_assert(aac_pow34sf_tab[220] < aac_pow2sf_tab[220],
                  "pow34sf < pow2sf for i > 200");
    static_assert(aac_pow34sf_tab[250] < aac_pow2sf_tab[250],
                  "pow34sf < pow2sf for i > 200 (2)");

    // Test that pow34sf > pow2sf for indices < 200 (since x^(3/4) > x when 0 < x < 1)
    static_assert(aac_pow34sf_tab[150] > aac_pow2sf_tab[150],
                  "pow34sf > pow2sf for i < 200");

    // Test exp2_lut values
    static_assert(exp2_lut[4] >= 1.18f && exp2_lut[4] <= 1.20f,
                  "exp2_lut[4] ≈ 2^(1/4)");
    static_assert(exp2_lut[12] >= 1.68f && exp2_lut[12] <= 1.69f,
                  "exp2_lut[12] ≈ 2^(3/4)");

    // Test zero entries are not zero
    static_assert(aac_pow2sf_tab[0] != 0.0f, "pow2sf[0] non-zero");
    static_assert(aac_pow34sf_tab[0] != 0.0f, "pow34sf[0] non-zero");

    // Test coverage: multiple indices
    static_assert(aac_pow2sf_tab[100] != 0.0f, "Coverage pow2sf[100]");
    static_assert(aac_pow2sf_tab[300] != 0.0f, "Coverage pow2sf[300]");
    static_assert(aac_pow34sf_tab[100] != 0.0f, "Coverage pow34sf[100]");
    static_assert(aac_pow34sf_tab[300] != 0.0f, "Coverage pow34sf[300]");

    // Test ratio relationship at index 200 (where both = 1)
    // pow2sf[200] / pow34sf[200] ≈ 1
    static_assert(aac_pow2sf_tab[200] / aac_pow34sf_tab[200] >= 0.99f &&
                  aac_pow2sf_tab[200] / aac_pow34sf_tab[200] <= 1.01f,
                  "Ratio at zero point ≈ 1");
}

/**
 * Constexpr accessors
 */
constexpr float get_aac_pow2sf_value(int i) noexcept {
    if (i < 0 || i >= TABLE_SIZE) return 0.0f;
    return aac_pow2sf_tab[i];
}

constexpr float get_aac_pow34sf_value(int i) noexcept {
    if (i < 0 || i >= TABLE_SIZE) return 0.0f;
    return aac_pow34sf_tab[i];
}

/**
 * Get raw table pointers for C interop
 */
inline const float* get_aac_pow2sf_tab() noexcept {
    return aac_pow2sf_tab.data();
}

inline const float* get_aac_pow34sf_tab() noexcept {
    return aac_pow34sf_tab.data();
}

} // namespace aac
} // namespace ffmpeg

#endif // AVCODEC_AAC_POW_TABLEGEN_CONSTEXPR_HPP
