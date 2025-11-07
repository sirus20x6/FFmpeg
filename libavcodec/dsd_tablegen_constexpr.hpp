/*
 * Modern C++ constexpr DSD conversion tables
 * Copyright (c) 2009 David Bender
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
 * Modern C++20 constexpr DSD (Direct Stream Digital) conversion tables
 *
 * DSD uses 1-bit delta-sigma modulation at very high sample rates (2.8224 MHz
 * for DSD64). Converting to PCM requires FIR filtering with specialized
 * lookup tables for efficient processing.
 *
 * Algorithm:
 * Pre-compute FIR filter outputs for all possible 8-bit input patterns.
 * Each bit represents +1 or -1, and the filter applies 48 symmetric taps.
 * The computation groups 8 bits at a time for "8 MACs" (multiply-accumulate)
 * operations, stored in lookup tables.
 *
 * Tables:
 * - ctables_msbf: MSB-first bit ordering [6][256] = 1,536 doubles
 * - ctables_lsbf: LSB-first bit ordering [6][256] = 1,536 doubles
 * - Total: 3,072 double-precision floating-point values
 *
 * CTABLES = 6 = ceil(48 / 8) groups of 8 filter taps each
 *
 * Used by: DSD audio decoder (Super Audio CD, .dff, .dsf files)
 */

#ifndef AVCODEC_DSD_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_DSD_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>

namespace ffmpeg {
namespace dsd {

// Constants from dsd.h
constexpr int HTAPS = 48;      // Number of FIR filter coefficients
constexpr int CTABLES = (HTAPS + 7) / 8;  // Number of "8 MACs" tables = 6

/**
 * FIR filter coefficients (from dsd.c)
 *
 * The 2nd half (48 coeffs) of a 96-tap symmetric lowpass filter.
 * Used for DSD to PCM conversion at 176.4 kHz (1/16 decimation from 2.8224 MHz).
 *
 * Symmetric filter property means first half mirrors second half,
 * so only 48 coefficients need to be stored.
 */
constexpr std::array<double, HTAPS> htaps = {
     0.09950731974056658,    0.09562845727714668,    0.08819647126516944,
     0.07782552527068175,    0.06534876523171299,    0.05172629311427257,
     0.0379429484910187,     0.02490921351762261,    0.0133774746265897,
     0.003883043418804416,  -0.003284703416210726,  -0.008080250212687497,
    -0.01067241812471033,   -0.01139427235000863,   -0.0106813877974587,
    -0.009007905078766049,  -0.006828859761015335,  -0.004535184322001496,
    -0.002425035959059578,  -0.0006922187080790708,  0.0005700762133516592,
     0.001353838005269448,   0.001713709169690937,   0.001742046839472948,
     0.001545601648013235,   0.001226696225277855,   0.0008704322683580222,
     0.0005381636200535649,  0.000266446345425276,   7.002968738383528e-05,
    -5.279407053811266e-05, -0.0001140625650874684, -0.0001304796361231895,
    -0.0001189970287491285, -9.396247155265073e-05, -6.577634378272832e-05,
    -4.07492895872535e-05,  -2.17407957554587e-05,  -9.163058931391722e-06,
    -2.017460145032201e-06,  1.249721855219005e-06,  2.166655190537392e-06,
     1.930520892991082e-06,  1.319400334374195e-06,  7.410039764949091e-07,
     3.423230509967409e-07,  1.244182214744588e-07,  3.130441005359396e-08
};

/**
 * 8-bit reversal lookup table (from libavutil/reverse.c)
 *
 * Reverses bit order: bit 0 ↔ bit 7, bit 1 ↔ bit 6, etc.
 * Used for LSB-first bit ordering in DSD streams.
 */
constexpr auto generate_reverse_table() noexcept {
    std::array<uint8_t, 256> table{};

    for (int i = 0; i < 256; ++i) {
        uint8_t reversed = 0;
        for (int bit = 0; bit < 8; ++bit) {
            if (i & (1 << bit)) {
                reversed |= (1 << (7 - bit));
            }
        }
        table[i] = reversed;
    }

    return table;
}

constexpr auto reverse_table = generate_reverse_table();

/**
 * Generate DSD conversion tables at compile time
 *
 * Algorithm (from dsd.c dsd_ctables_tableinit):
 *
 * for each 8-bit pattern e (0..255):
 *     Initialize acc[0..5] = 0
 *
 *     // Process 8 bits, each representing +1 or -1
 *     for bit position m (0..7):
 *         Extract bit: sign = ((e >> (7-m)) & 1) * 2 - 1  // Maps 0→-1, 1→+1
 *
 *         // Accumulate for each of 6 tables
 *         for table t (0..5):
 *             acc[t] += sign * htaps[t*8 + m]
 *
 *     // Store results in both orderings
 *     for table t (0..5):
 *         ctables_msbf[5-t][e] = acc[t]              // MSB-first
 *         ctables_lsbf[5-t][reverse[e]] = acc[t]      // LSB-first with reversal
 *
 * Mathematical interpretation:
 * Each table entry is a dot product between:
 * - Binary pattern e expanded to ±1 values
 * - 8 consecutive FIR filter coefficients
 *
 * This precomputes all possible 8-bit FIR filter outputs.
 *
 * Example: e=0b10110001 (0xB1 = 177)
 *   Bits: [1, 0, 1, 1, 0, 0, 0, 1] → Signs: [+1, -1, +1, +1, -1, -1, -1, +1]
 *   For table 0 (htaps[0..7]):
 *     result = htaps[0] - htaps[1] + htaps[2] + htaps[3]
 *            - htaps[4] - htaps[5] - htaps[6] + htaps[7]
 */
constexpr auto generate_dsd_ctables() noexcept {
    struct Tables {
        std::array<std::array<double, 256>, CTABLES> msbf{};
        std::array<std::array<double, 256>, CTABLES> lsbf{};
    };

    Tables tables;

    for (int e = 0; e < 256; ++e) {
        // Accumulator for each of the 6 tables
        std::array<double, CTABLES> acc{};

        // Process 8 bits of pattern e
        for (int m = 0; m < 8; ++m) {
            // Extract bit at position (7-m) and convert to ±1
            int bit = (e >> (7 - m)) & 1;
            int sign = bit * 2 - 1;  // Maps: 0→-1, 1→+1

            // Accumulate weighted filter coefficients
            for (int t = 0; t < CTABLES; ++t) {
                acc[t] += sign * htaps[t * 8 + m];
            }
        }

        // Store results in both bit orderings
        for (int t = 0; t < CTABLES; ++t) {
            // MSB-first: direct storage (reversed table index)
            tables.msbf[CTABLES - 1 - t][e] = acc[t];

            // LSB-first: with bit-reversed byte index
            tables.lsbf[CTABLES - 1 - t][reverse_table[e]] = acc[t];
        }
    }

    return tables;
}

// Generate tables at compile time
constexpr auto dsd_ctables = generate_dsd_ctables();

// Extract individual table references
constexpr auto& ctables_msbf = dsd_ctables.msbf;
constexpr auto& ctables_lsbf = dsd_ctables.lsbf;

// Total: 6 × 256 × 2 = 3,072 double values!

// Compile-time validation
namespace tests {
    // Test table dimensions
    static_assert(ctables_msbf.size() == CTABLES, "MSBF table size = 6");
    static_assert(ctables_lsbf.size() == CTABLES, "LSBF table size = 6");
    static_assert(ctables_msbf[0].size() == 256, "Table entries = 256");
    static_assert(ctables_lsbf[0].size() == 256, "Table entries = 256");

    // Test constants
    static_assert(HTAPS == 48, "HTAPS = 48");
    static_assert(CTABLES == 6, "CTABLES = 6");

    // Test htaps source data
    static_assert(htaps.size() == HTAPS, "htaps size = 48");
    static_assert(htaps[0] > 0.099 && htaps[0] < 0.100, "htaps[0] ≈ 0.0995");
    static_assert(htaps[47] > 0 && htaps[47] < 1e-7, "htaps[47] ≈ 3.13e-08");

    // Test reverse table
    static_assert(reverse_table.size() == 256, "Reverse table size");
    static_assert(reverse_table[0] == 0, "reverse(0x00) = 0x00");
    static_assert(reverse_table[1] == 0x80, "reverse(0x01) = 0x80");
    static_assert(reverse_table[0x80] == 1, "reverse(0x80) = 0x01");
    static_assert(reverse_table[0xFF] == 0xFF, "reverse(0xFF) = 0xFF");
    static_assert(reverse_table[0xAA] == 0x55, "reverse(0xAA) = 0x55");
    static_assert(reverse_table[0x55] == 0xAA, "reverse(0x55) = 0xAA");

    // Test that pattern 0x00 (all bits 0 = all -1) gives negative sums
    // Sum of 8 negative coefficients should be negative for most tables
    static_assert(ctables_msbf[0][0x00] < 0, "All -1 pattern negative");

    // Test that pattern 0xFF (all bits 1 = all +1) gives positive sums
    static_assert(ctables_msbf[0][0xFF] > 0, "All +1 pattern positive");

    // Test symmetry: pattern and its reverse should have same values in
    // corresponding tables
    static_assert(ctables_msbf[0][0xAA] == ctables_lsbf[0][0x55],
                  "MSB/LSB symmetry");

    // Test that different patterns produce different results
    static_assert(ctables_msbf[0][0x00] != ctables_msbf[0][0xFF],
                  "Different patterns differ");

    // Test value ranges (filter outputs should be bounded)
    static_assert(ctables_msbf[0][0xFF] < 1.0 && ctables_msbf[0][0xFF] > 0.0,
                  "Values in reasonable range");
    static_assert(ctables_msbf[0][0x00] > -1.0 && ctables_msbf[0][0x00] < 0.0,
                  "Negative values bounded");

    // Test coverage: multiple table indices
    static_assert(ctables_msbf[1][0x00] != 0.0, "Table 1 filled");
    static_assert(ctables_msbf[2][0x00] != 0.0, "Table 2 filled");
    static_assert(ctables_msbf[5][0x00] != 0.0, "Table 5 filled");

    // Test that MSBF and LSBF are actually different (not identical)
    static_assert(ctables_msbf[0][0x01] != ctables_lsbf[0][0x01],
                  "MSBF ≠ LSBF for non-symmetric patterns");

    // Test specific bit pattern computation
    // Pattern 0x80 (10000000b): only first bit set → +1, rest -1
    // For last table (table 5, htaps 40-47): most coeffs are tiny
    static_assert(ctables_msbf[CTABLES-1][0x80] != 0.0, "Pattern 0x80 computed");

    // Pattern 0x01 (00000001b): only last bit set → -1, -1, ..., -1, +1
    static_assert(ctables_msbf[CTABLES-1][0x01] != 0.0, "Pattern 0x01 computed");
}

/**
 * Constexpr accessors
 */
constexpr double get_dsd_ctable_value(bool lsbf, int table, int pattern) noexcept {
    if (table < 0 || table >= CTABLES || pattern < 0 || pattern >= 256) {
        return 0.0;
    }

    return lsbf ? ctables_lsbf[table][pattern] : ctables_msbf[table][pattern];
}

/**
 * Get table pointers for C interop
 */
inline const double (*get_dsd_ctables_msbf())[256] {
    return reinterpret_cast<const double(*)[256]>(ctables_msbf.data());
}

inline const double (*get_dsd_ctables_lsbf())[256] {
    return reinterpret_cast<const double(*)[256]>(ctables_lsbf.data());
}

} // namespace dsd
} // namespace ffmpeg

#endif // AVCODEC_DSD_TABLEGEN_CONSTEXPR_HPP
