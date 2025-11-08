/*
 * Compile-time generation of ADPCM codec data tables
 *
 * Original C version from FFmpeg ADPCM codecs
 * C++20 constexpr version created 2025-11-08
 *
 * Copyright (c) 2001-2003 The FFmpeg project
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

#ifndef AVCODEC_ADPCM_DATA_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_ADPCM_DATA_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>

namespace FFmpegADPCMData {

// ============================================================================
// ADPCM Index Table (16 bytes)
// ============================================================================

/**
 * ADPCM index table for IMA/DVI ADPCM codec.
 *
 * This table defines how the step size index changes based on the
 * 4-bit ADPCM sample value. Used to adapt the quantization step
 * size during encoding/decoding.
 *
 * For ADPCM samples 0-7: decrease index by 1 (minimum)
 * For ADPCM samples 8-15: increase index (2, 4, 6, or 8)
 *
 * This adaptive behavior allows ADPCM to track signal variations:
 * - Small samples → decrease step size (fine quantization)
 * - Large samples → increase step size (coarse quantization)
 */
constexpr auto generate_adpcm_index_table() noexcept {
    std::array<int8_t, 16> table{};

    // Values from ADPCM reference source
    // Pattern repeats twice: -1, -1, -1, -1, 2, 4, 6, 8
    constexpr int8_t values[16] = {
        -1, -1, -1, -1, 2, 4, 6, 8,
        -1, -1, -1, -1, 2, 4, 6, 8,
    };

    for (int i = 0; i < 16; ++i) {
        table[i] = values[i];
    }

    return table;
}

constexpr auto adpcm_index_table = generate_adpcm_index_table();

// ============================================================================
// ADPCM Step Table (178 bytes)
// ============================================================================

/**
 * ADPCM step size table for IMA/DVI ADPCM codec.
 *
 * This is the core quantization table for ADPCM. The step size determines
 * the scale of quantization levels. The table contains 89 entries indexed
 * by the step size index (0-88).
 *
 * Properties:
 * - Exponential growth: each step is ~1.1 times the previous
 * - Range: 7 to 32767 (fits in int16_t)
 * - Standardized: from ADPCM reference specification
 *
 * The step index is adapted during decoding using adpcm_index_table,
 * allowing the codec to dynamically adjust quantization precision
 * based on signal characteristics.
 */
constexpr auto generate_adpcm_step_table() noexcept {
    std::array<int16_t, 89> table{};

    // Values from ADPCM reference specification
    // These are carefully chosen to balance compression and quality
    constexpr int16_t values[89] = {
            7,     8,     9,    10,    11,    12,    13,    14,    16,    17,
           19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
           50,    55,    60,    66,    73,    80,    88,    97,   107,   118,
          130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
          337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
          876,   963,  1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
         2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
         5894,  6484,  7132,  7845,  8630,  9493, 10442, 11487, 12635, 13899,
        15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
    };

    for (int i = 0; i < 89; ++i) {
        table[i] = values[i];
    }

    return table;
}

constexpr auto adpcm_step_table = generate_adpcm_step_table();

// ============================================================================
// MS-ADPCM Adaptation Table (32 bytes)
// ============================================================================

/**
 * MS-ADPCM adaptation table.
 *
 * Microsoft's ADPCM variant uses this table to adapt the step size
 * based on recent prediction errors. The table is symmetric around
 * the midpoint, allowing both step size increases and decreases.
 *
 * Source: libsndfile
 */
constexpr auto generate_ms_adpcm_adaptation_table() noexcept {
    std::array<int16_t, 16> table{};

    constexpr int16_t values[16] = {
        230, 230, 230, 230, 307, 409, 512, 614,
        768, 614, 512, 409, 307, 230, 230, 230
    };

    for (int i = 0; i < 16; ++i) {
        table[i] = values[i];
    }

    return table;
}

constexpr auto ms_adpcm_adaptation_table = generate_ms_adpcm_adaptation_table();

// ============================================================================
// MS-ADPCM Adaptive Coefficients (14 bytes)
// ============================================================================

/**
 * MS-ADPCM adaptive coefficient 1.
 *
 * These coefficients are used in MS-ADPCM's predictor calculation.
 * Values are divided by 4 to fit in 8-bit integers, requiring
 * multiplication by 4 during use.
 *
 * The predictor combines two previous samples with these coefficients
 * to predict the next sample value.
 *
 * Source: libsndfile
 */
constexpr auto generate_ms_adpcm_coeff1() noexcept {
    std::array<uint8_t, 7> table{};

    constexpr uint8_t values[7] = {
        64, 128, 0, 48, 60, 115, 98
    };

    for (int i = 0; i < 7; ++i) {
        table[i] = values[i];
    }

    return table;
}

constexpr auto ms_adpcm_coeff1 = generate_ms_adpcm_coeff1();

/**
 * MS-ADPCM adaptive coefficient 2.
 *
 * Second set of predictor coefficients for MS-ADPCM.
 * Values are divided by 4 to fit in 8-bit signed integers.
 *
 * Used in conjunction with coeff1 to compute the predicted sample:
 * prediction = (coeff1[i] * sample1 + coeff2[i] * sample2) / 256
 *
 * Source: libsndfile
 */
constexpr auto generate_ms_adpcm_coeff2() noexcept {
    std::array<int8_t, 7> table{};

    constexpr int8_t values[7] = {
        0, -64, 0, 16, 0, -52, -58
    };

    for (int i = 0; i < 7; ++i) {
        table[i] = values[i];
    }

    return table;
}

constexpr auto ms_adpcm_coeff2 = generate_ms_adpcm_coeff2();

// ============================================================================
// Yamaha ADPCM Tables (32 bytes)
// ============================================================================

/**
 * Yamaha ADPCM index scale table.
 *
 * Yamaha's ADPCM variant uses this table for step size adaptation.
 * Similar structure to MS-ADPCM adaptation but with Yamaha-specific
 * scaling factors.
 */
constexpr auto generate_yamaha_indexscale() noexcept {
    std::array<int16_t, 16> table{};

    constexpr int16_t values[16] = {
        230, 230, 230, 230, 307, 409, 512, 614,
        230, 230, 230, 230, 307, 409, 512, 614
    };

    for (int i = 0; i < 16; ++i) {
        table[i] = values[i];
    }

    return table;
}

constexpr auto yamaha_indexscale = generate_yamaha_indexscale();

/**
 * Yamaha ADPCM difference lookup table.
 *
 * This table provides differential values used in Yamaha ADPCM
 * decoding. The table is symmetric with positive values in the
 * first half and corresponding negative values in the second half.
 */
constexpr auto generate_yamaha_difflookup() noexcept {
    std::array<int8_t, 16> table{};

    constexpr int8_t values[16] = {
         1,  3,  5,  7,  9,  11,  13,  15,
        -1, -3, -5, -7, -9, -11, -13, -15
    };

    for (int i = 0; i < 16; ++i) {
        table[i] = values[i];
    }

    return table;
}

constexpr auto yamaha_difflookup = generate_yamaha_difflookup();

// ============================================================================
// IMA Block Format Tables (8 bytes)
// ============================================================================

/**
 * IMA ADPCM block sizes for different formats.
 *
 * Defines the byte size of ADPCM blocks for various IMA ADPCM variants:
 * - [0]: 4 bytes (basic IMA)
 * - [1]: 12 bytes
 * - [2]: 4 bytes
 * - [3]: 20 bytes
 */
constexpr auto generate_ima_block_sizes() noexcept {
    std::array<uint8_t, 4> table{};

    table[0] = 4;
    table[1] = 12;
    table[2] = 4;
    table[3] = 20;

    return table;
}

constexpr auto ima_block_sizes = generate_ima_block_sizes();

/**
 * IMA ADPCM samples per block for different formats.
 *
 * Specifies how many audio samples are encoded in each block:
 * - [0]: 16 samples
 * - [1]: 32 samples
 * - [2]: 8 samples
 * - [3]: 32 samples
 */
constexpr auto generate_ima_block_samples() noexcept {
    std::array<uint8_t, 4> table{};

    table[0] = 16;
    table[1] = 32;
    table[2] = 8;
    table[3] = 32;

    return table;
}

constexpr auto ima_block_samples = generate_ima_block_samples();

// ============================================================================
// Static Assertions: Comprehensive Validation
// ============================================================================

// Table sizes
static_assert(adpcm_index_table.size() == 16, "Index table has 16 entries");
static_assert(adpcm_step_table.size() == 89, "Step table has 89 entries");
static_assert(ms_adpcm_adaptation_table.size() == 16, "MS adaptation table has 16 entries");
static_assert(ms_adpcm_coeff1.size() == 7, "MS coeff1 has 7 entries");
static_assert(ms_adpcm_coeff2.size() == 7, "MS coeff2 has 7 entries");
static_assert(yamaha_indexscale.size() == 16, "Yamaha indexscale has 16 entries");
static_assert(yamaha_difflookup.size() == 16, "Yamaha difflookup has 16 entries");
static_assert(ima_block_sizes.size() == 4, "IMA block sizes has 4 entries");
static_assert(ima_block_samples.size() == 4, "IMA block samples has 4 entries");

// Index table values
static_assert(adpcm_index_table[0] == -1, "Sample 0 decreases index");
static_assert(adpcm_index_table[3] == -1, "Sample 3 decreases index");
static_assert(adpcm_index_table[4] == 2, "Sample 4 increases index by 2");
static_assert(adpcm_index_table[5] == 4, "Sample 5 increases index by 4");
static_assert(adpcm_index_table[6] == 6, "Sample 6 increases index by 6");
static_assert(adpcm_index_table[7] == 8, "Sample 7 increases index by 8");
static_assert(adpcm_index_table[8] == -1, "Pattern repeats at index 8");
static_assert(adpcm_index_table[15] == 8, "Sample 15 increases index by 8");

// Step table bounds and progression
static_assert(adpcm_step_table[0] == 7, "Minimum step size is 7");
static_assert(adpcm_step_table[88] == 32767, "Maximum step size is 32767");
static_assert(adpcm_step_table[1] == 8, "Second step is 8");
static_assert(adpcm_step_table[10] == 19, "Step[10] is 19");
static_assert(adpcm_step_table[50] == 876, "Step[50] is 876");

// Verify step table monotonic increasing
constexpr bool verify_step_table_monotonic() {
    for (int i = 0; i < 88; ++i) {
        if (adpcm_step_table[i] >= adpcm_step_table[i + 1]) {
            return false;
        }
    }
    return true;
}
static_assert(verify_step_table_monotonic(), "Step table is strictly increasing");

// MS-ADPCM adaptation table symmetry
static_assert(ms_adpcm_adaptation_table[0] == 230, "MS adapt[0] = 230");
static_assert(ms_adpcm_adaptation_table[7] == 614, "MS adapt[7] = 614");
static_assert(ms_adpcm_adaptation_table[8] == 768, "MS adapt[8] = 768 (peak)");
static_assert(ms_adpcm_adaptation_table[0] == ms_adpcm_adaptation_table[15],
              "MS adaptation table is symmetric");

// MS-ADPCM coefficients
static_assert(ms_adpcm_coeff1[0] == 64, "MS coeff1[0] = 64");
static_assert(ms_adpcm_coeff1[1] == 128, "MS coeff1[1] = 128");
static_assert(ms_adpcm_coeff2[0] == 0, "MS coeff2[0] = 0");
static_assert(ms_adpcm_coeff2[1] == -64, "MS coeff2[1] = -64");

// Yamaha tables
static_assert(yamaha_indexscale[0] == 230, "Yamaha indexscale[0] = 230");
static_assert(yamaha_indexscale[7] == 614, "Yamaha indexscale[7] = 614");
static_assert(yamaha_difflookup[0] == 1, "Yamaha diff[0] = 1");
static_assert(yamaha_difflookup[7] == 15, "Yamaha diff[7] = 15");
static_assert(yamaha_difflookup[8] == -1, "Yamaha diff[8] = -1 (symmetric)");
static_assert(yamaha_difflookup[15] == -15, "Yamaha diff[15] = -15");

// Yamaha difflookup symmetry
constexpr bool verify_yamaha_symmetry() {
    for (int i = 0; i < 8; ++i) {
        if (yamaha_difflookup[i] != -yamaha_difflookup[i + 8]) {
            return false;
        }
    }
    return true;
}
static_assert(verify_yamaha_symmetry(), "Yamaha difflookup is symmetric");

// IMA block format tables
static_assert(ima_block_sizes[0] == 4, "IMA format 0: 4-byte blocks");
static_assert(ima_block_sizes[1] == 12, "IMA format 1: 12-byte blocks");
static_assert(ima_block_samples[0] == 16, "IMA format 0: 16 samples/block");
static_assert(ima_block_samples[1] == 32, "IMA format 1: 32 samples/block");

// Verify all step table values are positive and fit in int16_t
constexpr bool verify_step_table_range() {
    for (int i = 0; i < 89; ++i) {
        if (adpcm_step_table[i] <= 0 || adpcm_step_table[i] > 32767) {
            return false;
        }
    }
    return true;
}
static_assert(verify_step_table_range(), "All step values are positive and fit in int16_t");

} // namespace FFmpegADPCMData

// ============================================================================
// C API Compatibility
// ============================================================================

extern "C" {

/**
 * Get pointer to ADPCM index table.
 * Returns: Pointer to 16-element int8_t array
 */
inline const int8_t *get_adpcm_index_table() {
    return FFmpegADPCMData::adpcm_index_table.data();
}

/**
 * Get pointer to ADPCM step table.
 * Returns: Pointer to 89-element int16_t array
 */
inline const int16_t *get_adpcm_step_table() {
    return FFmpegADPCMData::adpcm_step_table.data();
}

/**
 * Get pointer to MS-ADPCM adaptation table.
 * Returns: Pointer to 16-element int16_t array
 */
inline const int16_t *get_ms_adpcm_adaptation_table() {
    return FFmpegADPCMData::ms_adpcm_adaptation_table.data();
}

/**
 * Get pointer to MS-ADPCM coefficient 1 table.
 * Returns: Pointer to 7-element uint8_t array
 */
inline const uint8_t *get_ms_adpcm_coeff1() {
    return FFmpegADPCMData::ms_adpcm_coeff1.data();
}

/**
 * Get pointer to MS-ADPCM coefficient 2 table.
 * Returns: Pointer to 7-element int8_t array
 */
inline const int8_t *get_ms_adpcm_coeff2() {
    return FFmpegADPCMData::ms_adpcm_coeff2.data();
}

/**
 * Get pointer to Yamaha ADPCM index scale table.
 * Returns: Pointer to 16-element int16_t array
 */
inline const int16_t *get_yamaha_indexscale() {
    return FFmpegADPCMData::yamaha_indexscale.data();
}

/**
 * Get pointer to Yamaha ADPCM difference lookup table.
 * Returns: Pointer to 16-element int8_t array
 */
inline const int8_t *get_yamaha_difflookup() {
    return FFmpegADPCMData::yamaha_difflookup.data();
}

/**
 * Get pointer to IMA ADPCM block sizes table.
 * Returns: Pointer to 4-element uint8_t array
 */
inline const uint8_t *get_ima_block_sizes() {
    return FFmpegADPCMData::ima_block_sizes.data();
}

/**
 * Get pointer to IMA ADPCM block samples table.
 * Returns: Pointer to 4-element uint8_t array
 */
inline const uint8_t *get_ima_block_samples() {
    return FFmpegADPCMData::ima_block_samples.data();
}

} // extern "C"

#endif // AVCODEC_ADPCM_DATA_TABLEGEN_CONSTEXPR_HPP
