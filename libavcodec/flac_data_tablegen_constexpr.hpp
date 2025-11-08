/*
 * FLAC data tables - C++20 constexpr implementation
 * Copyright (c) 2003 Alex Beregszaszi
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

#ifndef AVCODEC_FLAC_DATA_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_FLAC_DATA_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>

/**
 * @file
 * @brief FLAC codec parameter lookup tables
 *
 * This header provides compile-time generation of FLAC (Free Lossless Audio
 * Codec) parameter tables using C++20 constexpr. FLAC is a popular lossless
 * audio compression format for high-quality audio storage and streaming.
 *
 * Tables:
 * - sample_rate_table: Maps 4-bit codes to sample rates (Hz)
 * - blocksize_table: Maps 4-bit codes to block sizes (samples per block)
 *
 * Generated at compile time - zero runtime overhead.
 * All tables placed in .rodata section by the compiler.
 *
 * Data size: 128 bytes (32 entries total)
 * Static assertions: 30+ compile-time validations
 */

namespace FFmpegFLACData {

// ============================================================================
// Sample Rate Table (16 entries, 64 bytes)
// ============================================================================

/**
 * FLAC sample rate lookup table
 *
 * Maps 4-bit sample rate codes (0-15) to actual sample rates in Hz:
 * - 0: Get from STREAMINFO metadata block
 * - 1-11: Standard sample rates (8 kHz to 192 kHz)
 * - 12-14: Reserved (get from stream header)
 * - 15: Invalid
 *
 * Common sample rates:
 * - 44100 Hz (CD audio)
 * - 48000 Hz (professional audio, video)
 * - 88200, 96000, 176400, 192000 Hz (high-resolution audio)
 */
constexpr auto generate_flac_sample_rate_table() noexcept {
    std::array<int32_t, 16> table{};

    table[0]  = 0;       // get from STREAMINFO
    table[1]  = 88200;   // 88.2 kHz (2× CD)
    table[2]  = 176400;  // 176.4 kHz (4× CD)
    table[3]  = 192000;  // 192 kHz (high-res)
    table[4]  = 8000;    // 8 kHz (telephone)
    table[5]  = 16000;   // 16 kHz (wideband speech)
    table[6]  = 22050;   // 22.05 kHz (half CD)
    table[7]  = 24000;   // 24 kHz
    table[8]  = 32000;   // 32 kHz (broadcast)
    table[9]  = 44100;   // 44.1 kHz (CD standard)
    table[10] = 48000;   // 48 kHz (professional)
    table[11] = 96000;   // 96 kHz (high-res)
    table[12] = 0;       // get from stream header (kHz)
    table[13] = 0;       // get from stream header (×10 Hz)
    table[14] = 0;       // get from stream header (Hz)
    table[15] = 0;       // invalid

    return table;
}

constexpr auto flac_sample_rate_table = generate_flac_sample_rate_table();

// ============================================================================
// Block Size Table (16 entries, 64 bytes)
// ============================================================================

/**
 * FLAC block size lookup table
 *
 * Maps 4-bit blocksize codes (0-15) to block sizes in samples:
 * - 0: Reserved
 * - 1: 192 samples (special case)
 * - 2-5: 576 × 2^(n-2) samples (576, 1152, 2304, 4608)
 * - 6-7: Get from stream header
 * - 8-15: 256 × 2^(n-8) samples (256, 512, 1024, ..., 32768)
 *
 * Common block sizes:
 * - 1152 samples (most common for general audio)
 * - 4096 samples (common for high quality)
 */
constexpr auto generate_flac_blocksize_table() noexcept {
    std::array<int32_t, 16> table{};

    table[0]  = 0;          // reserved
    table[1]  = 192;        // special case
    table[2]  = 576 << 0;   // 576
    table[3]  = 576 << 1;   // 1152 (most common)
    table[4]  = 576 << 2;   // 2304
    table[5]  = 576 << 3;   // 4608
    table[6]  = 0;          // get 8-bit from end of header
    table[7]  = 0;          // get 16-bit from end of header
    table[8]  = 256 << 0;   // 256
    table[9]  = 256 << 1;   // 512
    table[10] = 256 << 2;   // 1024
    table[11] = 256 << 3;   // 2048
    table[12] = 256 << 4;   // 4096 (common for HQ)
    table[13] = 256 << 5;   // 8192
    table[14] = 256 << 6;   // 16384
    table[15] = 256 << 7;   // 32768

    return table;
}

constexpr auto flac_blocksize_table = generate_flac_blocksize_table();

} // namespace FFmpegFLACData

// ============================================================================
// Static Assertions - Compile-Time Validation
// ============================================================================

// Sample rate table validation
static_assert(FFmpegFLACData::flac_sample_rate_table.size() == 16,
              "Sample rate table must have 16 entries");
static_assert(FFmpegFLACData::flac_sample_rate_table[0] == 0,
              "Code 0: get from STREAMINFO");

// CD and professional audio rates
static_assert(FFmpegFLACData::flac_sample_rate_table[9] == 44100,
              "Code 9: CD audio (44.1 kHz)");
static_assert(FFmpegFLACData::flac_sample_rate_table[10] == 48000,
              "Code 10: Professional audio (48 kHz)");

// High-resolution audio rates
static_assert(FFmpegFLACData::flac_sample_rate_table[1] == 88200,
              "Code 1: 2× CD (88.2 kHz)");
static_assert(FFmpegFLACData::flac_sample_rate_table[2] == 176400,
              "Code 2: 4× CD (176.4 kHz)");
static_assert(FFmpegFLACData::flac_sample_rate_table[11] == 96000,
              "Code 11: High-res (96 kHz)");
static_assert(FFmpegFLACData::flac_sample_rate_table[3] == 192000,
              "Code 12: High-res (192 kHz)");

// Telephony and speech rates
static_assert(FFmpegFLACData::flac_sample_rate_table[4] == 8000,
              "Code 4: Telephone quality (8 kHz)");
static_assert(FFmpegFLACData::flac_sample_rate_table[5] == 16000,
              "Code 5: Wideband speech (16 kHz)");

// Other standard rates
static_assert(FFmpegFLACData::flac_sample_rate_table[6] == 22050,
              "Code 6: Half CD (22.05 kHz)");
static_assert(FFmpegFLACData::flac_sample_rate_table[7] == 24000,
              "Code 7: 24 kHz");
static_assert(FFmpegFLACData::flac_sample_rate_table[8] == 32000,
              "Code 8: Broadcast (32 kHz)");

// Reserved codes
static_assert(FFmpegFLACData::flac_sample_rate_table[12] == 0,
              "Code 12: get from header");
static_assert(FFmpegFLACData::flac_sample_rate_table[13] == 0,
              "Code 13: get from header");
static_assert(FFmpegFLACData::flac_sample_rate_table[14] == 0,
              "Code 14: get from header");
static_assert(FFmpegFLACData::flac_sample_rate_table[15] == 0,
              "Code 15: invalid");

// Block size table validation
static_assert(FFmpegFLACData::flac_blocksize_table.size() == 16,
              "Block size table must have 16 entries");
static_assert(FFmpegFLACData::flac_blocksize_table[0] == 0,
              "Code 0: reserved");
static_assert(FFmpegFLACData::flac_blocksize_table[1] == 192,
              "Code 1: special 192-sample block");

// 576-based block sizes (codes 2-5)
static_assert(FFmpegFLACData::flac_blocksize_table[2] == 576,
              "Code 2: 576 samples");
static_assert(FFmpegFLACData::flac_blocksize_table[3] == 1152,
              "Code 3: 1152 samples (most common)");
static_assert(FFmpegFLACData::flac_blocksize_table[4] == 2304,
              "Code 4: 2304 samples");
static_assert(FFmpegFLACData::flac_blocksize_table[5] == 4608,
              "Code 5: 4608 samples");

// Verify 576-based doubling pattern
static_assert(FFmpegFLACData::flac_blocksize_table[3] ==
              FFmpegFLACData::flac_blocksize_table[2] * 2,
              "576-based: each code doubles size");
static_assert(FFmpegFLACData::flac_blocksize_table[4] ==
              FFmpegFLACData::flac_blocksize_table[3] * 2,
              "576-based progression");
static_assert(FFmpegFLACData::flac_blocksize_table[5] ==
              FFmpegFLACData::flac_blocksize_table[4] * 2,
              "576-based progression");

// Header-defined sizes
static_assert(FFmpegFLACData::flac_blocksize_table[6] == 0,
              "Code 6: 8-bit from header");
static_assert(FFmpegFLACData::flac_blocksize_table[7] == 0,
              "Code 7: 16-bit from header");

// 256-based block sizes (codes 8-15)
static_assert(FFmpegFLACData::flac_blocksize_table[8] == 256,
              "Code 8: 256 samples");
static_assert(FFmpegFLACData::flac_blocksize_table[9] == 512,
              "Code 9: 512 samples");
static_assert(FFmpegFLACData::flac_blocksize_table[10] == 1024,
              "Code 10: 1024 samples");
static_assert(FFmpegFLACData::flac_blocksize_table[11] == 2048,
              "Code 11: 2048 samples");
static_assert(FFmpegFLACData::flac_blocksize_table[12] == 4096,
              "Code 12: 4096 samples (common HQ)");
static_assert(FFmpegFLACData::flac_blocksize_table[13] == 8192,
              "Code 13: 8192 samples");
static_assert(FFmpegFLACData::flac_blocksize_table[14] == 16384,
              "Code 14: 16384 samples");
static_assert(FFmpegFLACData::flac_blocksize_table[15] == 32768,
              "Code 15: 32768 samples (maximum)");

// Verify 256-based doubling pattern
static_assert(FFmpegFLACData::flac_blocksize_table[9] ==
              FFmpegFLACData::flac_blocksize_table[8] * 2,
              "256-based: each code doubles size");
static_assert(FFmpegFLACData::flac_blocksize_table[10] ==
              FFmpegFLACData::flac_blocksize_table[9] * 2,
              "256-based progression");
static_assert(FFmpegFLACData::flac_blocksize_table[15] ==
              FFmpegFLACData::flac_blocksize_table[8] * 128,
              "256-based: 2^7 growth from 256 to 32768");

// Cross-validation: verify pattern relationships
static_assert(FFmpegFLACData::flac_blocksize_table[3] * 2 ==
              FFmpegFLACData::flac_blocksize_table[4],
              "Doubling pattern: 1152 → 2304");
static_assert(FFmpegFLACData::flac_blocksize_table[12] ==
              FFmpegFLACData::flac_blocksize_table[10] * 4,
              "Power-of-2: 1024 × 4 = 4096");

// ============================================================================
// C API Compatibility Layer
// ============================================================================

extern "C" {

/**
 * Get pointer to FLAC sample rate table
 * @return Pointer to 16-entry int32_t array
 */
inline const int32_t* get_flac_sample_rate_table() {
    return FFmpegFLACData::flac_sample_rate_table.data();
}

/**
 * Get pointer to FLAC block size table
 * @return Pointer to 16-entry int32_t array
 */
inline const int32_t* get_flac_blocksize_table() {
    return FFmpegFLACData::flac_blocksize_table.data();
}

} // extern "C"

#endif // AVCODEC_FLAC_DATA_TABLEGEN_CONSTEXPR_HPP
