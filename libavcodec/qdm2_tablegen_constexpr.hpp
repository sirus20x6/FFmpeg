/*
 * Modern C++ constexpr QDM2 codec lookup tables
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
 * Modern C++20 constexpr QDM2 audio codec lookup tables
 *
 * This header provides compile-time generation of QDM2 codec tables:
 * - Softclip table: Smooth clipping using sine function
 * - Noise tables: Pseudo-random noise generation
 * - Dequantization tables: Random index mapping
 *
 * QDM2 (QDesign Music 2) is an audio codec that uses various lookup
 * tables for audio processing. The softclip table provides smooth
 * clipping behavior using a sine-based curve.
 *
 * All tables generated at compile time from their generation algorithms.
 */

#ifndef AVCODEC_QDM2_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_QDM2_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>
#include <cmath>

namespace ffmpeg {
namespace qdm2 {

// Constants from original QDM2 code
constexpr int SOFTCLIP_THRESHOLD = 27600;
constexpr int HARDCLIP_THRESHOLD = 35716;
constexpr int SOFTCLIP_TABLE_SIZE = HARDCLIP_THRESHOLD - SOFTCLIP_THRESHOLD + 1;  // 8117

// Noise table sizes
constexpr int NOISE_TABLE_SIZE = 4096 + 20;  // 4116
constexpr int NOISE_SAMPLES_SIZE = 128;

// Dequantization table sizes
constexpr int DEQUANT_INDEX_SIZE = 256;
constexpr int DEQUANT_INDEX_DEPTH = 5;
constexpr int DEQUANT_TYPE24_SIZE = 128;
constexpr int DEQUANT_TYPE24_DEPTH = 3;

/**
 * Reuse sine implementation from sinewin
 * (or implement Taylor series here if needed)
 */
constexpr double PI = 3.14159265358979323846;

constexpr double sin_constexpr(double x) noexcept {
    // Normalize to [-π, π]
    while (x > PI) x -= 2.0 * PI;
    while (x < -PI) x += 2.0 * PI;

    // Taylor series for sine
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
 * Generate soft-clipping table
 *
 * The soft-clip table provides smooth audio clipping behavior.
 * It uses a sine-based curve to transition smoothly from linear
 * to hard clipping.
 *
 * Formula:
 *   softclip[i] = SOFTCLIP_THRESHOLD - sin(i * delta) * dfl
 *   where dfl = SOFTCLIP_THRESHOLD - 32767
 *         delta = 1.0 / -dfl
 *
 * This creates a smooth S-curve for audio limiting.
 *
 * @return Array of 8117 uint16_t values
 */
constexpr auto generate_softclip_table() noexcept {
    std::array<uint16_t, SOFTCLIP_TABLE_SIZE> table{};

    constexpr double dfl = SOFTCLIP_THRESHOLD - 32767;  // Negative value
    constexpr double delta = 1.0 / -dfl;

    for (int i = 0; i < SOFTCLIP_TABLE_SIZE; ++i) {
        double sine_val = sin_constexpr(static_cast<double>(i) * delta);
        int result = SOFTCLIP_THRESHOLD - static_cast<int>(sine_val * dfl);
        table[i] = static_cast<uint16_t>(result & 0xFFFF);
    }

    return table;
}

/**
 * Linear Congruential Generator for pseudo-random numbers
 * Constants from original QDM2 code: a=214013, c=2531011
 */
constexpr uint64_t lcg_next(uint64_t seed) noexcept {
    return seed * 214013 + 2531011;
}

/**
 * Generate noise table using LCG
 *
 * Creates pseudo-random noise samples in range [-1.3, 1.3].
 * Uses a Linear Congruential Generator for reproducible noise.
 *
 * @return Array of 4116 float noise values
 */
constexpr auto generate_noise_table() noexcept {
    std::array<float, NOISE_TABLE_SIZE> table{};

    uint64_t random_seed = 0;
    constexpr float delta = 1.0f / 16384.0f;

    for (int i = 0; i < 4096; ++i) {
        random_seed = lcg_next(random_seed);

        // Extract 15-bit random value
        int32_t random_15bit = static_cast<int32_t>(random_seed >> 16) & 0x7FFF;

        // Scale to [-1.0, 1.0] range, then multiply by 1.3
        float noise = (delta * static_cast<float>(random_15bit) - 1.0f) * 1.3f;
        table[i] = noise;
    }

    // Remaining 20 entries stay zero (padding)
    for (int i = 4096; i < NOISE_TABLE_SIZE; ++i) {
        table[i] = 0.0f;
    }

    return table;
}

/**
 * Generate random dequantization index table
 *
 * Maps 256 input values to 5-element dequantization indices.
 * Uses base-3 decomposition with seeds [81, 27, 9, 3, 1].
 *
 * Algorithm:
 *   For each index i (0-255):
 *     decompose i in base-3 using divisors [81, 27, 9, 3, 1]
 *
 * @return 256x5 array of uint8_t indices
 */
constexpr auto generate_random_dequant_index() noexcept {
    std::array<std::array<uint8_t, DEQUANT_INDEX_DEPTH>, DEQUANT_INDEX_SIZE> table{};

    for (int i = 0; i < DEQUANT_INDEX_SIZE; ++i) {
        uint32_t ldw = i;
        uint32_t divisor = 81;  // 3^4

        for (int j = 0; j < DEQUANT_INDEX_DEPTH; ++j) {
            table[i][j] = static_cast<uint8_t>(ldw / divisor);
            ldw %= divisor;
            divisor /= 3;
        }
    }

    return table;
}

/**
 * Generate random dequantization type24 table
 *
 * Maps 128 input values to 3-element type24 indices.
 * Uses base-5 decomposition with seeds [25, 5, 1].
 *
 * Algorithm:
 *   For each index i (0-127):
 *     decompose i in base-5 using divisors [25, 5, 1]
 *
 * @return 128x3 array of uint8_t indices
 */
constexpr auto generate_random_dequant_type24() noexcept {
    std::array<std::array<uint8_t, DEQUANT_TYPE24_DEPTH>, DEQUANT_TYPE24_SIZE> table{};

    for (int i = 0; i < DEQUANT_TYPE24_SIZE; ++i) {
        uint32_t ldw = i;
        uint32_t divisor = 25;  // 5^2

        for (int j = 0; j < DEQUANT_TYPE24_DEPTH; ++j) {
            table[i][j] = static_cast<uint8_t>(ldw / divisor);
            ldw %= divisor;
            divisor /= 5;
        }
    }

    return table;
}

/**
 * Generate noise samples using LCG
 *
 * Creates 128 pseudo-random noise samples in range [-1.0, 1.0].
 * Uses same LCG as noise table but different seed.
 *
 * @return Array of 128 float noise samples
 */
constexpr auto generate_noise_samples() noexcept {
    std::array<float, NOISE_SAMPLES_SIZE> samples{};

    uint32_t random_seed = 0;
    constexpr float delta = 1.0f / 16384.0f;

    for (int i = 0; i < NOISE_SAMPLES_SIZE; ++i) {
        random_seed = static_cast<uint32_t>(lcg_next(random_seed));

        // Extract 15-bit random value
        uint32_t random_15bit = (random_seed >> 16) & 0x7FFF;

        // Scale to [-1.0, 1.0] range
        samples[i] = delta * static_cast<float>(random_15bit) - 1.0f;
    }

    return samples;
}

// Generate all tables at compile time
constexpr auto softclip_table = generate_softclip_table();
constexpr auto noise_table = generate_noise_table();
constexpr auto random_dequant_index = generate_random_dequant_index();
constexpr auto random_dequant_type24 = generate_random_dequant_type24();
constexpr auto noise_samples = generate_noise_samples();

// Total: 8,117 + 4,116 + 1,280 + 384 + 128 = 14,025 entries!

// Compile-time validation
namespace tests {
    // Test table sizes
    static_assert(softclip_table.size() == SOFTCLIP_TABLE_SIZE, "Softclip table size");
    static_assert(SOFTCLIP_TABLE_SIZE == 8117, "Softclip size constant");

    static_assert(noise_table.size() == NOISE_TABLE_SIZE, "Noise table size");
    static_assert(NOISE_TABLE_SIZE == 4116, "Noise table size constant");

    static_assert(random_dequant_index.size() == DEQUANT_INDEX_SIZE, "Dequant index size");
    static_assert(random_dequant_type24.size() == DEQUANT_TYPE24_SIZE, "Dequant type24 size");
    static_assert(noise_samples.size() == NOISE_SAMPLES_SIZE, "Noise samples size");

    // Test softclip table properties
    // First value should be near SOFTCLIP_THRESHOLD
    static_assert(softclip_table[0] >= SOFTCLIP_THRESHOLD - 100 &&
                  softclip_table[0] <= SOFTCLIP_THRESHOLD + 100,
                  "Softclip start value");

    // Last value should be near HARDCLIP_THRESHOLD
    static_assert(softclip_table[SOFTCLIP_TABLE_SIZE - 1] >= HARDCLIP_THRESHOLD - 100 &&
                  softclip_table[SOFTCLIP_TABLE_SIZE - 1] <= HARDCLIP_THRESHOLD + 100,
                  "Softclip end value");

    // Table should be monotonic (always increasing)
    static_assert(softclip_table[100] < softclip_table[200], "Softclip monotonic 1");
    static_assert(softclip_table[200] < softclip_table[300], "Softclip monotonic 2");
    static_assert(softclip_table[1000] < softclip_table[2000], "Softclip monotonic 3");

    // Test noise table properties
    // Noise values should be in range [-1.3, 1.3]
    static_assert(noise_table[0] >= -1.4f && noise_table[0] <= 1.4f, "Noise range 1");
    static_assert(noise_table[100] >= -1.4f && noise_table[100] <= 1.4f, "Noise range 2");
    static_assert(noise_table[1000] >= -1.4f && noise_table[1000] <= 1.4f, "Noise range 3");

    // Last 20 entries should be zero (padding)
    static_assert(noise_table[4096] == 0.0f, "Noise padding start");
    static_assert(noise_table[4100] == 0.0f, "Noise padding mid");
    static_assert(noise_table[NOISE_TABLE_SIZE - 1] == 0.0f, "Noise padding end");

    // Test dequantization index table
    // First row (index 0) should be all zeros
    static_assert(random_dequant_index[0][0] == 0, "Dequant index [0][0]");
    static_assert(random_dequant_index[0][1] == 0, "Dequant index [0][1]");
    static_assert(random_dequant_index[0][2] == 0, "Dequant index [0][2]");

    // Test base-3 decomposition: 81 = 81/81=1, rest zeros
    static_assert(random_dequant_index[81][0] == 1, "Dequant index base-3");
    static_assert(random_dequant_index[81][1] == 0, "Dequant index base-3");

    // Test base-3: 27 = 0*81 + 1*27 + ...
    static_assert(random_dequant_index[27][0] == 0, "Dequant index 27");
    static_assert(random_dequant_index[27][1] == 1, "Dequant index 27");

    // Test dequantization type24 table
    // First row should be all zeros
    static_assert(random_dequant_type24[0][0] == 0, "Dequant type24 [0][0]");
    static_assert(random_dequant_type24[0][1] == 0, "Dequant type24 [0][1]");
    static_assert(random_dequant_type24[0][2] == 0, "Dequant type24 [0][2]");

    // Test base-5 decomposition: 25 = 1*25 + 0*5 + 0
    static_assert(random_dequant_type24[25][0] == 1, "Dequant type24 base-5");
    static_assert(random_dequant_type24[25][1] == 0, "Dequant type24 base-5");
    static_assert(random_dequant_type24[25][2] == 0, "Dequant type24 base-5");

    // Test noise samples
    // Should be in range [-1.0, 1.0]
    static_assert(noise_samples[0] >= -1.1f && noise_samples[0] <= 1.1f, "Noise sample range");
    static_assert(noise_samples[64] >= -1.1f && noise_samples[64] <= 1.1f, "Noise sample range mid");

    // Test LCG produces different values
    constexpr uint64_t lcg1 = lcg_next(0);
    constexpr uint64_t lcg2 = lcg_next(lcg1);
    constexpr uint64_t lcg3 = lcg_next(lcg2);
    static_assert(lcg1 != 0, "LCG produces non-zero");
    static_assert(lcg2 != lcg1, "LCG produces different values");
    static_assert(lcg3 != lcg2, "LCG continues sequence");

    // Test sine function accuracy (reused from sinewin)
    constexpr double sin_0 = sin_constexpr(0.0);
    static_assert(sin_0 >= -0.01 && sin_0 <= 0.01, "sin(0) ≈ 0");

    constexpr double sin_pi_6 = sin_constexpr(PI / 6.0);
    static_assert(sin_pi_6 >= 0.49 && sin_pi_6 <= 0.51, "sin(π/6) ≈ 0.5");
}

/**
 * Constexpr accessors for table data
 */
constexpr uint16_t softclip_lookup(int index) noexcept {
    return (index >= 0 && index < SOFTCLIP_TABLE_SIZE)
        ? softclip_table[index]
        : 0;
}

constexpr float noise_lookup(int index) noexcept {
    return (index >= 0 && index < NOISE_TABLE_SIZE)
        ? noise_table[index]
        : 0.0f;
}

constexpr uint8_t dequant_index_lookup(int i, int j) noexcept {
    return (i >= 0 && i < DEQUANT_INDEX_SIZE && j >= 0 && j < DEQUANT_INDEX_DEPTH)
        ? random_dequant_index[i][j]
        : 0;
}

} // namespace qdm2
} // namespace ffmpeg

#endif // AVCODEC_QDM2_TABLEGEN_CONSTEXPR_HPP
