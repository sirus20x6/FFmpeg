/*
 * Modern C++ constexpr COOK pow2 tables
 * Copyright (c) 2003 Sascha Sommer
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
 * Modern C++20 constexpr COOK codec power-of-2 tables
 *
 * COOK is a lossy audio codec developed by RealNetworks (formerly RealAudio G2/Cooker).
 * It uses modified discrete cosine transform (MDCT) with gain control and requires
 * efficient power-of-2 computations for dequantization and gain application.
 *
 * Tables:
 * - pow2tab[127]: Computes 2^i for -63 ≤ i < 64
 * - rootpow2tab[127]: Computes 2^(i/2) for -63 ≤ i < 64
 *
 * Both tables use index mapping: table[63 + i] = f(i)
 *
 * Algorithm:
 * pow2tab: Straightforward - start with 2^(-63), double each iteration
 * rootpow2tab: Clever interleaving:
 *   - Start with 2^(-32) = 2^(-63/2 + 0.5)
 *   - Every even i: multiply by 2 (moves to next whole power)
 *   - Alternate between ×1 and ×√2 for half-steps
 *
 * Total: 254 float entries (127 × 2)
 *
 * Used by: COOK audio decoder (RealAudio G2, .ra, .rm files)
 */

#ifndef AVCODEC_COOK_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_COOK_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cmath>

namespace ffmpeg {
namespace cook {

// Constants
constexpr int TABLE_SIZE = 127;
constexpr int INDEX_OFFSET = 63;  // Maps i ∈ [-63, 64) to [0, 127)
constexpr double SQRT2_VAL = 1.41421356237309504880;  // √2

/**
 * Constexpr exp2 function (2^x)
 *
 * Uses Taylor series around x = 0:
 * 2^x = e^(x·ln(2)) = 1 + (x·ln(2)) + (x·ln(2))²/2! + ...
 *
 * For integer x, more efficient to use repeated multiplication.
 */
constexpr double exp2_constexpr(double x) noexcept {
    constexpr double LN2 = 0.69314718055994530942;  // ln(2)

    // For exact integer powers, use multiplication (more accurate)
    if (x == static_cast<int>(x)) {
        int n = static_cast<int>(x);
        bool neg = n < 0;
        if (neg) n = -n;

        double result = 1.0;
        double base = 2.0;

        while (n > 0) {
            if (n & 1) result *= base;
            base *= base;
            n >>= 1;
        }

        return neg ? (1.0 / result) : result;
    }

    // For fractional powers, use exp(x·ln2) Taylor series
    double x_ln2 = x * LN2;
    double result = 1.0;
    double term = 1.0;

    for (int i = 1; i <= 20; ++i) {
        term *= x_ln2 / i;
        result += term;
    }

    return result;
}

/**
 * Generate pow2tab at compile time
 *
 * Computes 2^i for -63 ≤ i < 64
 * Stored at index [63 + i], so:
 *   pow2tab[0] = 2^(-63)  (smallest)
 *   pow2tab[63] = 2^0 = 1 (middle)
 *   pow2tab[126] = 2^63   (largest)
 *
 * Algorithm: Start with 2^(-63), double each step.
 */
constexpr auto generate_pow2tab() noexcept {
    std::array<float, TABLE_SIZE> table{};

    // Start with 2^(-63) = 1 / 2^63
    double exp2_val = exp2_constexpr(-63);

    for (int i = -63; i < 64; ++i) {
        table[INDEX_OFFSET + i] = static_cast<float>(exp2_val);
        exp2_val *= 2.0;  // Next power: 2^(i+1) = 2^i · 2
    }

    return table;
}

/**
 * Generate rootpow2tab at compile time
 *
 * Computes 2^(i/2) for -63 ≤ i < 64
 * Stored at index [63 + i]
 *
 * Algorithm (from cook.c):
 * - Start with root_val = 2^(-32) = 2^((-63+1)/2) = 2^(-31)
 * - Wait, that's not quite right. Let me re-analyze...
 *
 * Actually: root_val = 2^(-32) = 2^(-64/2)
 * But we want 2^(-63/2) for i=-63
 *
 * The trick: 2^(-63/2) = 2^(-32 + 0.5) = 2^(-32) · 2^(0.5) = 2^(-32) · √2
 *
 * Pattern:
 * - i even: 2^(i/2) = 2^(i/2)
 * - i odd: 2^(i/2) = 2^((i-1)/2) · 2^(0.5) = 2^((i-1)/2) · √2
 *
 * Iterative algorithm:
 * - When i transitions from odd to even (i & 1 == 0), multiply by 2
 * - Alternate between ×1 and ×√2 based on i parity
 *
 * Let's trace a few values:
 * i = -63 (odd):  root_val = 2^(-32) · √2 = 2^(-31.5) ✓
 * i = -62 (even): root_val = 2^(-32) · √2, then ×2 = 2^(-30.5)... wait
 *
 * Actually from code:
 *   if (!(i & 1)) root_val *= 2;  // if even, double first
 *   rootpow2tab[63+i] = root_val * exp2_tab[i & 1];
 *   where exp2_tab = {1, √2}
 *
 * So: root_val tracks 2^(floor(i/2))
 */
constexpr auto generate_rootpow2tab() noexcept {
    std::array<float, TABLE_SIZE> table{};

    constexpr double exp2_tab[2] = {1.0, SQRT2_VAL};

    // root_val tracks 2^(floor(i/2))
    // Start: i = -63, floor(-63/2) = -32, so root_val = 2^(-32)
    double root_val = exp2_constexpr(-32);

    for (int i = -63; i < 64; ++i) {
        // If i is even, we've moved to next floor value
        if ((i & 1) == 0) {
            root_val *= 2.0;
        }

        // Compute 2^(i/2):
        // i even: i/2 = floor(i/2), so use root_val · 1
        // i odd: i/2 = floor(i/2) + 0.5, so use root_val · √2
        table[INDEX_OFFSET + i] = static_cast<float>(root_val * exp2_tab[i & 1]);
    }

    return table;
}

// Generate tables at compile time
constexpr auto pow2tab = generate_pow2tab();
constexpr auto rootpow2tab = generate_rootpow2tab();

// Total: 127 + 127 = 254 float entries!

// Compile-time validation
namespace tests {
    // Test table sizes
    static_assert(pow2tab.size() == TABLE_SIZE, "pow2tab size = 127");
    static_assert(rootpow2tab.size() == TABLE_SIZE, "rootpow2tab size = 127");

    // Test constants
    static_assert(TABLE_SIZE == 127, "Table size constant");
    static_assert(INDEX_OFFSET == 63, "Index offset constant");

    // Test pow2tab key values
    // pow2tab[63] should be 2^0 = 1
    static_assert(pow2tab[63] >= 0.99f && pow2tab[63] <= 1.01f,
                  "pow2tab[63] ≈ 1 (2^0)");

    // pow2tab[64] should be 2^1 = 2
    static_assert(pow2tab[64] >= 1.99f && pow2tab[64] <= 2.01f,
                  "pow2tab[64] ≈ 2 (2^1)");

    // pow2tab[0] should be 2^(-63) (very small)
    static_assert(pow2tab[0] > 0.0f && pow2tab[0] < 1e-15f,
                  "pow2tab[0] ≈ 2^(-63) (very small)");

    // pow2tab[126] should be 2^63 (very large)
    static_assert(pow2tab[126] > 9.0e18f,
                  "pow2tab[126] ≈ 2^63 (very large)");

    // Test rootpow2tab key values
    // rootpow2tab[63] should be 2^(0/2) = 2^0 = 1
    static_assert(rootpow2tab[63] >= 0.99f && rootpow2tab[63] <= 1.01f,
                  "rootpow2tab[63] ≈ 1 (2^0)");

    // rootpow2tab[64] should be 2^(1/2) = √2 ≈ 1.414
    static_assert(rootpow2tab[64] >= 1.41f && rootpow2tab[64] <= 1.42f,
                  "rootpow2tab[64] ≈ √2 (2^0.5)");

    // rootpow2tab[65] should be 2^(2/2) = 2^1 = 2
    static_assert(rootpow2tab[65] >= 1.99f && rootpow2tab[65] <= 2.01f,
                  "rootpow2tab[65] ≈ 2 (2^1)");

    // Test doubling property for pow2tab
    static_assert(pow2tab[64] / pow2tab[63] >= 1.99f && pow2tab[64] / pow2tab[63] <= 2.01f,
                  "pow2tab doubles each step");

    // Test sqrt(2) relationship for rootpow2tab
    // rootpow2tab[64] / rootpow2tab[63] should be √2
    static_assert(rootpow2tab[64] / rootpow2tab[63] >= 1.41f &&
                  rootpow2tab[64] / rootpow2tab[63] <= 1.42f,
                  "rootpow2tab increases by √2");

    // Test that rootpow2tab[i+2] ≈ rootpow2tab[i] · 2
    static_assert(rootpow2tab[65] / rootpow2tab[63] >= 1.99f &&
                  rootpow2tab[65] / rootpow2tab[63] <= 2.01f,
                  "rootpow2tab doubles every 2 steps");

    // Test range coverage
    static_assert(pow2tab[0] != 0.0f, "pow2tab[0] non-zero");
    static_assert(pow2tab[126] != 0.0f, "pow2tab[126] non-zero");
    static_assert(rootpow2tab[0] != 0.0f, "rootpow2tab[0] non-zero");
    static_assert(rootpow2tab[126] != 0.0f, "rootpow2tab[126] non-zero");

    // Test monotonicity (both tables should be strictly increasing)
    static_assert(pow2tab[1] > pow2tab[0], "pow2tab monotonic");
    static_assert(pow2tab[63] > pow2tab[62], "pow2tab monotonic middle");
    static_assert(pow2tab[126] > pow2tab[125], "pow2tab monotonic end");

    static_assert(rootpow2tab[1] > rootpow2tab[0], "rootpow2tab monotonic");
    static_assert(rootpow2tab[63] > rootpow2tab[62], "rootpow2tab monotonic middle");
    static_assert(rootpow2tab[126] > rootpow2tab[125], "rootpow2tab monotonic end");

    // Test mathematical relationship: pow2tab[i]² ≈ pow2tab[i*2]
    // pow2tab[32] = 2^(-31), pow2tab[64] = 2^1
    // (2^(-31))² = 2^(-62) = pow2tab[1]
    // Let's test: pow2tab[62]² ≈ pow2tab[124]
    // 2^(-1)² = 2^(-2) ✓
    static_assert(pow2tab[62] * pow2tab[62] >= pow2tab[61] * 0.99f &&
                  pow2tab[62] * pow2tab[62] <= pow2tab[61] * 1.01f,
                  "pow2tab square relationship");

    // Test relationship: rootpow2tab[i]² ≈ pow2tab[i]
    // 2^(i/2))² = 2^i ✓
    static_assert(rootpow2tab[64] * rootpow2tab[64] >= pow2tab[64] * 0.99f &&
                  rootpow2tab[64] * rootpow2tab[64] <= pow2tab[64] * 1.01f,
                  "rootpow2tab² = pow2tab");
}

/**
 * Constexpr accessors
 */
constexpr float get_pow2_value(int i) noexcept {
    if (i < -63 || i >= 64) return 0.0f;
    return pow2tab[INDEX_OFFSET + i];
}

constexpr float get_rootpow2_value(int i) noexcept {
    if (i < -63 || i >= 64) return 0.0f;
    return rootpow2tab[INDEX_OFFSET + i];
}

/**
 * Get raw table pointers for C interop
 */
inline const float* get_pow2tab() noexcept {
    return pow2tab.data();
}

inline const float* get_rootpow2tab() noexcept {
    return rootpow2tab.data();
}

} // namespace cook
} // namespace ffmpeg

#endif // AVCODEC_COOK_TABLEGEN_CONSTEXPR_HPP
