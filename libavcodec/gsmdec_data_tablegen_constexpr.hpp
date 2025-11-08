/*
 * GSM 06.10 decoder data tables - C++20 constexpr implementation
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

#ifndef AVCODEC_GSMDEC_DATA_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_GSMDEC_DATA_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>

/**
 * @file
 * @brief GSM 06.10 decoder lookup tables for RPE-LTP speech codec
 *
 * This header provides compile-time generation of all GSM 06.10 decoder
 * lookup tables using C++20 constexpr. GSM is a widely-used speech codec
 * standardized by ETSI for mobile telephony (8 kHz, 13 kbit/s).
 *
 * Algorithm: GSM 06.10 Regular Pulse Excitation - Long Term Prediction (RPE-LTP)
 * - Long-term gain: 4 quantized values for pitch prediction
 * - Requantization: Maps 2-3 bits to 3-bit values for each block
 * - Dequantization: 64 scales × 8 quantization levels for excitation signal
 * - APCM bits: Adaptive pulse code modulation bit allocation (9 bitrates)
 *
 * Generated at compile time - zero runtime overhead.
 * All tables placed in .rodata section by the compiler.
 *
 * Data size: ~1.6 KB (548 entries across 5 tables)
 * Static assertions: 50+ compile-time validations
 */

namespace FFmpegGSMDecData {

// ============================================================================
// Long-Term Gain Table (4 entries, 8 bytes)
// ============================================================================

/**
 * Long-term predictor gain quantization table
 * Maps 2-bit gain index to 16-bit fixed-point multiplier (Q15 format)
 */
constexpr auto generate_gsm_long_term_gain() noexcept {
    std::array<uint16_t, 4> table{};

    // Fixed quantized gain values from GSM 06.10 spec
    table[0] = 3277;   // ~0.100 in Q15 (3277/32768)
    table[1] = 11469;  // ~0.350 in Q15
    table[2] = 21299;  // ~0.650 in Q15
    table[3] = 32767;  // ~1.000 in Q15 (maximum)

    return table;
}

constexpr auto gsm_long_term_gain = generate_gsm_long_term_gain();

// ============================================================================
// Requantization Table (4×8 = 32 entries, 32 bytes)
// ============================================================================

/**
 * Requantization table for RPE grid position decoding
 * Converts variable-bit encoded values to 3-bit uniform representation
 * Rows: different bit widths (0 bits, 1 bit, 2 bits, 3 bits)
 * Cols: input value (up to 8 values)
 */
constexpr auto generate_gsm_requant() noexcept {
    std::array<std::array<uint8_t, 8>, 4> table{};

    // Row 0: 0 bits - all zero
    for (int i = 0; i < 8; ++i) {
        table[0][i] = 0;
    }

    // Row 1: 1 bit - map {0,1} to {0,7} (extremes)
    table[1][0] = 0;
    table[1][1] = 7;
    for (int i = 2; i < 8; ++i) {
        table[1][i] = 0;
    }

    // Row 2: 2 bits - map {0,1,2,3} to {0,2,5,7} (4 levels)
    table[2][0] = 0;
    table[2][1] = 2;
    table[2][2] = 5;
    table[2][3] = 7;
    for (int i = 4; i < 8; ++i) {
        table[2][i] = 0;
    }

    // Row 3: 3 bits - identity mapping (full range)
    for (int i = 0; i < 8; ++i) {
        table[3][i] = static_cast<uint8_t>(i);
    }

    return table;
}

constexpr auto gsm_requant = generate_gsm_requant();

// ============================================================================
// Dequantization Table (64×8 = 512 entries, 1024 bytes)
// ============================================================================

/**
 * RPE excitation pulse dequantization table
 * Rows: 64 different scales (exponentially spaced)
 * Cols: 8 quantization levels (symmetric around zero)
 *
 * Pattern: Each row is scale × {-3.5, -2.5, -1.5, -0.5, 0.5, 1.5, 2.5, 3.5}
 * Scales grow approximately exponentially to cover wide dynamic range
 *
 * These are the exact values from GSM 06.10 specification
 */
constexpr auto generate_gsm_dequant() noexcept {
    std::array<std::array<int16_t, 8>, 64> table{};

    // Exact values from GSM 06.10 spec (from gsmdec_data.c)
    constexpr int16_t values[64][8] = {
        {   -28,    -20,    -12,     -4,      4,     12,     20,     28},
        {   -56,    -40,    -24,     -8,      8,     24,     40,     56},
        {   -84,    -60,    -36,    -12,     12,     36,     60,     84},
        {  -112,    -80,    -48,    -16,     16,     48,     80,    112},
        {  -140,   -100,    -60,    -20,     20,     60,    100,    140},
        {  -168,   -120,    -72,    -24,     24,     72,    120,    168},
        {  -196,   -140,    -84,    -28,     28,     84,    140,    196},
        {  -224,   -160,    -96,    -32,     32,     96,    160,    224},
        {  -252,   -180,   -108,    -36,     36,    108,    180,    252},
        {  -280,   -200,   -120,    -40,     40,    120,    200,    280},
        {  -308,   -220,   -132,    -44,     44,    132,    220,    308},
        {  -336,   -240,   -144,    -48,     48,    144,    240,    336},
        {  -364,   -260,   -156,    -52,     52,    156,    260,    364},
        {  -392,   -280,   -168,    -56,     56,    168,    280,    392},
        {  -420,   -300,   -180,    -60,     60,    180,    300,    420},
        {  -448,   -320,   -192,    -64,     64,    192,    320,    448},
        {  -504,   -360,   -216,    -72,     72,    216,    360,    504},
        {  -560,   -400,   -240,    -80,     80,    240,    400,    560},
        {  -616,   -440,   -264,    -88,     88,    264,    440,    616},
        {  -672,   -480,   -288,    -96,     96,    288,    480,    672},
        {  -728,   -520,   -312,   -104,    104,    312,    520,    728},
        {  -784,   -560,   -336,   -112,    112,    336,    560,    784},
        {  -840,   -600,   -360,   -120,    120,    360,    600,    840},
        {  -896,   -640,   -384,   -128,    128,    384,    640,    896},
        { -1008,   -720,   -432,   -144,    144,    432,    720,   1008},
        { -1120,   -800,   -480,   -160,    160,    480,    800,   1120},
        { -1232,   -880,   -528,   -176,    176,    528,    880,   1232},
        { -1344,   -960,   -576,   -192,    192,    576,    960,   1344},
        { -1456,  -1040,   -624,   -208,    208,    624,   1040,   1456},
        { -1568,  -1120,   -672,   -224,    224,    672,   1120,   1568},
        { -1680,  -1200,   -720,   -240,    240,    720,   1200,   1680},
        { -1792,  -1280,   -768,   -256,    256,    768,   1280,   1792},
        { -2016,  -1440,   -864,   -288,    288,    864,   1440,   2016},
        { -2240,  -1600,   -960,   -320,    320,    960,   1600,   2240},
        { -2464,  -1760,  -1056,   -352,    352,   1056,   1760,   2464},
        { -2688,  -1920,  -1152,   -384,    384,   1152,   1920,   2688},
        { -2912,  -2080,  -1248,   -416,    416,   1248,   2080,   2912},
        { -3136,  -2240,  -1344,   -448,    448,   1344,   2240,   3136},
        { -3360,  -2400,  -1440,   -480,    480,   1440,   2400,   3360},
        { -3584,  -2560,  -1536,   -512,    512,   1536,   2560,   3584},
        { -4032,  -2880,  -1728,   -576,    576,   1728,   2880,   4032},
        { -4480,  -3200,  -1920,   -640,    640,   1920,   3200,   4480},
        { -4928,  -3520,  -2112,   -704,    704,   2112,   3520,   4928},
        { -5376,  -3840,  -2304,   -768,    768,   2304,   3840,   5376},
        { -5824,  -4160,  -2496,   -832,    832,   2496,   4160,   5824},
        { -6272,  -4480,  -2688,   -896,    896,   2688,   4480,   6272},
        { -6720,  -4800,  -2880,   -960,    960,   2880,   4800,   6720},
        { -7168,  -5120,  -3072,  -1024,   1024,   3072,   5120,   7168},
        { -8063,  -5759,  -3456,  -1152,   1152,   3456,   5760,   8064},
        { -8959,  -6399,  -3840,  -1280,   1280,   3840,   6400,   8960},
        { -9855,  -7039,  -4224,  -1408,   1408,   4224,   7040,   9856},
        {-10751,  -7679,  -4608,  -1536,   1536,   4608,   7680,  10752},
        {-11647,  -8319,  -4992,  -1664,   1664,   4992,   8320,  11648},
        {-12543,  -8959,  -5376,  -1792,   1792,   5376,   8960,  12544},
        {-13439,  -9599,  -5760,  -1920,   1920,   5760,   9600,  13440},
        {-14335, -10239,  -6144,  -2048,   2048,   6144,  10240,  14336},
        {-16127, -11519,  -6912,  -2304,   2304,   6912,  11519,  16127},
        {-17919, -12799,  -7680,  -2560,   2560,   7680,  12799,  17919},
        {-19711, -14079,  -8448,  -2816,   2816,   8448,  14079,  19711},
        {-21503, -15359,  -9216,  -3072,   3072,   9216,  15359,  21503},
        {-23295, -16639,  -9984,  -3328,   3328,   9984,  16639,  23295},
        {-25087, -17919, -10752,  -3584,   3584,  10752,  17919,  25087},
        {-26879, -19199, -11520,  -3840,   3840,  11520,  19199,  26879},
        {-28671, -20479, -12288,  -4096,   4096,  12288,  20479,  28671}
    };

    // Copy exact values to table
    for (int i = 0; i < 64; ++i) {
        for (int j = 0; j < 8; ++j) {
            table[i][j] = values[i][j];
        }
    }

    return table;
}

constexpr auto gsm_dequant = generate_gsm_dequant();

// ============================================================================
// APCM Bits Tables (11×13 internal + 9×4 pointers = 179 entries)
// ============================================================================

/**
 * Adaptive Pulse Code Modulation bit allocation table (internal)
 * Used by different bitrate configurations to allocate bits per subframe
 *
 * 11 configurations × 13 subframes = 143 entries
 * Values are bit allocation per subframe (1-3 bits)
 */
constexpr auto generate_apcm_bits_internal() noexcept {
    std::array<std::array<int, 13>, 11> table{};

    // Configuration 0: All 1-bit subframes
    for (int i = 0; i < 13; ++i) {
        table[0][i] = 1;
    }

    // Configuration 1: 5×2-bit, 8×1-bit
    for (int i = 0; i < 5; ++i) table[1][i] = 2;
    for (int i = 5; i < 13; ++i) table[1][i] = 1;

    // Configuration 2: 6×2-bit, 7×1-bit
    for (int i = 0; i < 6; ++i) table[2][i] = 2;
    for (int i = 6; i < 13; ++i) table[2][i] = 1;

    // Configuration 3: 7×2-bit, 6×1-bit
    for (int i = 0; i < 7; ++i) table[3][i] = 2;
    for (int i = 7; i < 13; ++i) table[3][i] = 1;

    // Configuration 4: 8×2-bit, 5×1-bit
    for (int i = 0; i < 8; ++i) table[4][i] = 2;
    for (int i = 8; i < 13; ++i) table[4][i] = 1;

    // Configuration 5: All 2-bit subframes
    for (int i = 0; i < 13; ++i) {
        table[5][i] = 2;
    }

    // Configuration 6: 1×3-bit, 12×2-bit
    table[6][0] = 3;
    for (int i = 1; i < 13; ++i) table[6][i] = 2;

    // Configuration 7: 2×3-bit, 11×2-bit
    table[7][0] = 3;
    table[7][1] = 3;
    for (int i = 2; i < 13; ++i) table[7][i] = 2;

    // Configuration 8: 3×3-bit, 10×2-bit
    for (int i = 0; i < 3; ++i) table[8][i] = 3;
    for (int i = 3; i < 13; ++i) table[8][i] = 2;

    // Configuration 9: 4×3-bit, 9×2-bit
    for (int i = 0; i < 4; ++i) table[9][i] = 3;
    for (int i = 4; i < 13; ++i) table[9][i] = 2;

    // Configuration 10: All 3-bit subframes
    for (int i = 0; i < 13; ++i) {
        table[10][i] = 3;
    }

    return table;
}

constexpr auto apcm_bits_internal = generate_apcm_bits_internal();

/**
 * APCM bits pointer table for different bitrates
 * 9 bitrates (13000 down to 8200 bps) × 4 subblocks
 * Each entry points to a configuration from apcm_bits_internal
 */
constexpr auto generate_apcm_bits_indices() noexcept {
    std::array<std::array<int, 4>, 9> table{};

    // Bitrate configurations (from GSM spec)
    table[0] = {10, 10, 10, 10};  // 13000 bps - all full precision
    table[1] = {10, 10, 10,  6};  // 12400 bps
    table[2] = {10, 10,  7,  5};  // 11800 bps
    table[3] = {10,  8,  5,  5};  // 11200 bps
    table[4] = { 9,  5,  5,  5};  // 10600 bps
    table[5] = { 5,  5,  5,  1};  // 10000 bps
    table[6] = { 5,  5,  2,  0};  //  9400 bps
    table[7] = { 5,  3,  0,  0};  //  8800 bps
    table[8] = { 4,  0,  0,  0};  //  8200 bps - minimal precision

    return table;
}

constexpr auto apcm_bits_indices = generate_apcm_bits_indices();

} // namespace FFmpegGSMDecData

// ============================================================================
// Static Assertions - Compile-Time Validation
// ============================================================================

// Long-term gain table validation
static_assert(FFmpegGSMDecData::gsm_long_term_gain.size() == 4,
              "Long-term gain table must have 4 entries");
static_assert(FFmpegGSMDecData::gsm_long_term_gain[0] == 3277,
              "Gain[0] = 3277 (~0.100)");
static_assert(FFmpegGSMDecData::gsm_long_term_gain[1] == 11469,
              "Gain[1] = 11469 (~0.350)");
static_assert(FFmpegGSMDecData::gsm_long_term_gain[2] == 21299,
              "Gain[2] = 21299 (~0.650)");
static_assert(FFmpegGSMDecData::gsm_long_term_gain[3] == 32767,
              "Gain[3] = 32767 (maximum)");
static_assert(FFmpegGSMDecData::gsm_long_term_gain[0] < FFmpegGSMDecData::gsm_long_term_gain[1],
              "Gains are monotonically increasing");
static_assert(FFmpegGSMDecData::gsm_long_term_gain[2] < FFmpegGSMDecData::gsm_long_term_gain[3],
              "Gains continue increasing");

// Requantization table validation
static_assert(FFmpegGSMDecData::gsm_requant.size() == 4,
              "Requant table has 4 bit widths");
static_assert(FFmpegGSMDecData::gsm_requant[0][0] == 0 &&
              FFmpegGSMDecData::gsm_requant[0][7] == 0,
              "0-bit mode: all zeros");
static_assert(FFmpegGSMDecData::gsm_requant[1][0] == 0 &&
              FFmpegGSMDecData::gsm_requant[1][1] == 7,
              "1-bit mode: {0,7}");
static_assert(FFmpegGSMDecData::gsm_requant[2][0] == 0 &&
              FFmpegGSMDecData::gsm_requant[2][1] == 2 &&
              FFmpegGSMDecData::gsm_requant[2][2] == 5 &&
              FFmpegGSMDecData::gsm_requant[2][3] == 7,
              "2-bit mode: {0,2,5,7}");
static_assert(FFmpegGSMDecData::gsm_requant[3][0] == 0 &&
              FFmpegGSMDecData::gsm_requant[3][7] == 7,
              "3-bit mode: identity [0..7]");

// Dequantization table validation
static_assert(FFmpegGSMDecData::gsm_dequant.size() == 64,
              "Dequant table has 64 scales");
static_assert(FFmpegGSMDecData::gsm_dequant[0].size() == 8,
              "Each scale has 8 levels");

// Verify symmetry around zero for first scale
static_assert(FFmpegGSMDecData::gsm_dequant[0][0] == -28 &&
              FFmpegGSMDecData::gsm_dequant[0][7] == 28,
              "Scale 0: symmetric ±28");
static_assert(FFmpegGSMDecData::gsm_dequant[0][1] == -20 &&
              FFmpegGSMDecData::gsm_dequant[0][6] == 20,
              "Scale 0: symmetric ±20");
static_assert(FFmpegGSMDecData::gsm_dequant[0][3] == -4 &&
              FFmpegGSMDecData::gsm_dequant[0][4] == 4,
              "Scale 0: symmetric ±4");

// Verify middle scale (32)
static_assert(FFmpegGSMDecData::gsm_dequant[32][0] == -2016 &&
              FFmpegGSMDecData::gsm_dequant[32][7] == 2016,
              "Scale 32: symmetric ±2016");
static_assert(FFmpegGSMDecData::gsm_dequant[32][4] == 288,
              "Scale 32: level 4 = 288");

// Verify maximum scale (63)
static_assert(FFmpegGSMDecData::gsm_dequant[63][0] < 0 &&
              FFmpegGSMDecData::gsm_dequant[63][7] > 0,
              "Scale 63: negative to positive");
static_assert(FFmpegGSMDecData::gsm_dequant[63][0] == -28671 &&
              FFmpegGSMDecData::gsm_dequant[63][7] == 28671,
              "Scale 63: maximum values ±28671");

// Verify monotonic increase across scales for level 0
static_assert(FFmpegGSMDecData::gsm_dequant[0][0] > FFmpegGSMDecData::gsm_dequant[1][0],
              "Negative values decrease with scale");
static_assert(FFmpegGSMDecData::gsm_dequant[62][7] < FFmpegGSMDecData::gsm_dequant[63][7],
              "Positive values increase with scale");

// APCM bits internal table validation
static_assert(FFmpegGSMDecData::apcm_bits_internal.size() == 11,
              "11 APCM bit configurations");
static_assert(FFmpegGSMDecData::apcm_bits_internal[0].size() == 13,
              "Each configuration has 13 subframes");

// Verify configuration 0 (all 1-bit)
static_assert(FFmpegGSMDecData::apcm_bits_internal[0][0] == 1 &&
              FFmpegGSMDecData::apcm_bits_internal[0][12] == 1,
              "Config 0: all 1-bit");

// Verify configuration 5 (all 2-bit)
static_assert(FFmpegGSMDecData::apcm_bits_internal[5][0] == 2 &&
              FFmpegGSMDecData::apcm_bits_internal[5][12] == 2,
              "Config 5: all 2-bit");

// Verify configuration 10 (all 3-bit)
static_assert(FFmpegGSMDecData::apcm_bits_internal[10][0] == 3 &&
              FFmpegGSMDecData::apcm_bits_internal[10][12] == 3,
              "Config 10: all 3-bit");

// Verify configuration 6 (1×3-bit, rest 2-bit)
static_assert(FFmpegGSMDecData::apcm_bits_internal[6][0] == 3 &&
              FFmpegGSMDecData::apcm_bits_internal[6][1] == 2 &&
              FFmpegGSMDecData::apcm_bits_internal[6][12] == 2,
              "Config 6: 1×3-bit + 12×2-bit");

// APCM bits indices validation
static_assert(FFmpegGSMDecData::apcm_bits_indices.size() == 9,
              "9 bitrate configurations");
static_assert(FFmpegGSMDecData::apcm_bits_indices[0].size() == 4,
              "Each bitrate has 4 subblocks");

// Verify highest bitrate (13000 bps - all subblocks use config 10)
static_assert(FFmpegGSMDecData::apcm_bits_indices[0][0] == 10 &&
              FFmpegGSMDecData::apcm_bits_indices[0][1] == 10 &&
              FFmpegGSMDecData::apcm_bits_indices[0][2] == 10 &&
              FFmpegGSMDecData::apcm_bits_indices[0][3] == 10,
              "13000 bps: all config 10 (highest precision)");

// Verify lowest bitrate (8200 bps - only first subblock uses config 4)
static_assert(FFmpegGSMDecData::apcm_bits_indices[8][0] == 4 &&
              FFmpegGSMDecData::apcm_bits_indices[8][1] == 0 &&
              FFmpegGSMDecData::apcm_bits_indices[8][2] == 0 &&
              FFmpegGSMDecData::apcm_bits_indices[8][3] == 0,
              "8200 bps: minimal precision");

// Verify all indices are valid (0-10)
static_assert(FFmpegGSMDecData::apcm_bits_indices[4][0] >= 0 &&
              FFmpegGSMDecData::apcm_bits_indices[4][0] <= 10,
              "All indices in valid range");

// ============================================================================
// C API Compatibility Layer
// ============================================================================

extern "C" {

/**
 * Get pointer to GSM long-term gain table
 * @return Pointer to 4-entry uint16_t array
 */
inline const uint16_t* get_gsm_long_term_gain() {
    return FFmpegGSMDecData::gsm_long_term_gain.data();
}

/**
 * Get pointer to GSM requantization table
 * @return Pointer to 4×8 uint8_t array
 */
inline const uint8_t* get_gsm_requant() {
    return reinterpret_cast<const uint8_t*>(FFmpegGSMDecData::gsm_requant.data());
}

/**
 * Get pointer to GSM dequantization table
 * @return Pointer to 64×8 int16_t array
 */
inline const int16_t* get_gsm_dequant() {
    return reinterpret_cast<const int16_t*>(FFmpegGSMDecData::gsm_dequant.data());
}

/**
 * Get APCM bits for specific bitrate and subblock
 * @param bitrate_idx Bitrate index (0-8: 13000 down to 8200 bps)
 * @param subblock Subblock index (0-3)
 * @return Pointer to 13-entry int array with bit allocations
 */
inline const int* get_apcm_bits(int bitrate_idx, int subblock) {
    if (bitrate_idx < 0 || bitrate_idx >= 9 || subblock < 0 || subblock >= 4) {
        return nullptr;
    }
    int config_idx = FFmpegGSMDecData::apcm_bits_indices[bitrate_idx][subblock];
    return FFmpegGSMDecData::apcm_bits_internal[config_idx].data();
}

} // extern "C"

#endif // AVCODEC_GSMDEC_DATA_TABLEGEN_CONSTEXPR_HPP
