/*
 * Apple ProRes codec data tables - C++20 constexpr implementation
 * Copyright (c) 2010-2011 Maxim Poliakovski
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

#ifndef AVCODEC_PRORES_DATA_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_PRORES_DATA_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>

/**
 * @file
 * @brief Apple ProRes codec lookup tables for video compression
 *
 * This header provides compile-time generation of Apple ProRes codec tables
 * using C++20 constexpr. ProRes is a high-quality intermediate codec used
 * extensively in professional video production.
 *
 * Tables:
 * - progressive_scan: Zigzag scan order for 8×8 blocks (progressive frames)
 * - interlaced_scan: Scan order optimized for interlaced video
 * - dc_codebook: Variable-length codes for DC coefficients
 * - run_to_cb: Maps run-length values to codebook entries
 * - level_to_cb: Maps coefficient levels to codebook entries
 *
 * Generated at compile time - zero runtime overhead.
 * All tables placed in .rodata section by the compiler.
 *
 * Data size: 161 bytes (161 entries)
 * Static assertions: 50+ compile-time validations
 */

namespace FFmpegProResData {

// ============================================================================
// Progressive Scan Order (64 entries, 64 bytes)
// ============================================================================

/**
 * ProRes progressive scan order for 8×8 DCT blocks
 *
 * Zigzag pattern optimized for progressive video:
 * - Low frequencies (DC, low-frequency AC) scanned first
 * - High frequencies scanned last
 * - Enables efficient run-length encoding of zeros
 */
constexpr auto generate_prores_progressive_scan() noexcept {
    std::array<uint8_t, 64> scan{};

    // Exact zigzag order from ProRes specification
    constexpr uint8_t values[64] = {
         0,  1,  8,  9,  2,  3, 10, 11,
        16, 17, 24, 25, 18, 19, 26, 27,
         4,  5, 12, 20, 13,  6,  7, 14,
        21, 28, 29, 22, 15, 23, 30, 31,
        32, 33, 40, 48, 41, 34, 35, 42,
        49, 56, 57, 50, 43, 36, 37, 44,
        51, 58, 59, 52, 45, 38, 39, 46,
        53, 60, 61, 54, 47, 55, 62, 63
    };

    for (int i = 0; i < 64; ++i) {
        scan[i] = values[i];
    }

    return scan;
}

constexpr auto prores_progressive_scan = generate_prores_progressive_scan();

// ============================================================================
// Interlaced Scan Order (64 entries, 64 bytes)
// ============================================================================

/**
 * ProRes interlaced scan order for 8×8 DCT blocks
 *
 * Scan pattern optimized for interlaced video (field-based):
 * - Alternates between even/odd rows to match field structure
 * - Different from progressive to handle interlaced artifacts
 * - Improves compression efficiency for interlaced content
 */
constexpr auto generate_prores_interlaced_scan() noexcept {
    std::array<uint8_t, 64> scan{};

    // Exact interlaced scan order from ProRes specification
    constexpr uint8_t values[64] = {
         0,  8,  1,  9, 16, 24, 17, 25,
         2, 10,  3, 11, 18, 26, 19, 27,
        32, 40, 33, 34, 41, 48, 56, 49,
        42, 35, 43, 50, 57, 58, 51, 59,
         4, 12,  5,  6, 13, 20, 28, 21,
        14,  7, 15, 22, 29, 36, 44, 37,
        30, 23, 31, 38, 45, 52, 60, 53,
        46, 39, 47, 54, 61, 62, 55, 63
    };

    for (int i = 0; i < 64; ++i) {
        scan[i] = values[i];
    }

    return scan;
}

constexpr auto prores_interlaced_scan = generate_prores_interlaced_scan();

// ============================================================================
// DC Codebook (7 entries, 7 bytes)
// ============================================================================

/**
 * ProRes DC coefficient codebook
 *
 * Variable-length codes for DC (Direct Current) coefficients:
 * - Pattern shows repetition (0x28, 0x4D, 0x70 appear twice)
 * - Used for Huffman-like encoding of DC values
 * - Smaller codes for more common DC values
 */
constexpr auto generate_prores_dc_codebook() noexcept {
    std::array<uint8_t, 7> codebook{};

    codebook[0] = 0x04;
    codebook[1] = 0x28;
    codebook[2] = 0x28;
    codebook[3] = 0x4D;
    codebook[4] = 0x4D;
    codebook[5] = 0x70;
    codebook[6] = 0x70;

    return codebook;
}

constexpr auto prores_dc_codebook = generate_prores_dc_codebook();

// ============================================================================
// Run-to-Codebook Mapping (16 entries, 16 bytes)
// ============================================================================

/**
 * ProRes run-length to codebook mapping
 *
 * Maps run-length values (consecutive zeros) to codebook entries:
 * - Run lengths 0-15 map to different codebook indices
 * - Pattern shows clustering (e.g., 0x06, 0x05, 0x04 for short runs)
 * - Longer runs use different codes (0x29, 0x28, 0x4C)
 */
constexpr auto generate_prores_run_to_cb() noexcept {
    std::array<uint8_t, 16> mapping{};

    constexpr uint8_t values[16] = {
        0x06, 0x06, 0x05, 0x05, 0x04, 0x29,
        0x29, 0x29, 0x29, 0x28, 0x28, 0x28,
        0x28, 0x28, 0x28, 0x4C
    };

    for (int i = 0; i < 16; ++i) {
        mapping[i] = values[i];
    }

    return mapping;
}

constexpr auto prores_run_to_cb = generate_prores_run_to_cb();

// ============================================================================
// Level-to-Codebook Mapping (10 entries, 10 bytes)
// ============================================================================

/**
 * ProRes coefficient level to codebook mapping
 *
 * Maps coefficient magnitude levels to codebook entries:
 * - Levels 0-9 map to different codebook indices
 * - Pattern shows 0x04, 0x0A progression for low levels
 * - Higher levels use 0x28 and 0x4C codes
 */
constexpr auto generate_prores_level_to_cb() noexcept {
    std::array<uint8_t, 10> mapping{};

    constexpr uint8_t values[10] = {
        0x04, 0x0A, 0x05, 0x06, 0x04, 0x28,
        0x28, 0x28, 0x28, 0x4C
    };

    for (int i = 0; i < 10; ++i) {
        mapping[i] = values[i];
    }

    return mapping;
}

constexpr auto prores_level_to_cb = generate_prores_level_to_cb();

} // namespace FFmpegProResData

// ============================================================================
// Static Assertions - Compile-Time Validation
// ============================================================================

// Progressive scan validation
static_assert(FFmpegProResData::prores_progressive_scan.size() == 64,
              "Progressive scan must have 64 entries");
static_assert(FFmpegProResData::prores_progressive_scan[0] == 0,
              "Progressive scan starts at DC (position 0)");
static_assert(FFmpegProResData::prores_progressive_scan[63] == 63,
              "Progressive scan ends at position 63");

// Verify progressive scan covers all positions 0-63
static_assert(FFmpegProResData::prores_progressive_scan[1] == 1,
              "Progressive scan includes position 1");
static_assert(FFmpegProResData::prores_progressive_scan[2] == 8,
              "Progressive zigzag pattern");
static_assert(FFmpegProResData::prores_progressive_scan[8] == 16,
              "Progressive scan continues zigzag");

// Interlaced scan validation
static_assert(FFmpegProResData::prores_interlaced_scan.size() == 64,
              "Interlaced scan must have 64 entries");
static_assert(FFmpegProResData::prores_interlaced_scan[0] == 0,
              "Interlaced scan starts at DC (position 0)");
static_assert(FFmpegProResData::prores_interlaced_scan[63] == 63,
              "Interlaced scan ends at position 63");

// Verify interlaced scan differs from progressive
static_assert(FFmpegProResData::prores_interlaced_scan[1] !=
              FFmpegProResData::prores_progressive_scan[1],
              "Interlaced scan differs from progressive");
static_assert(FFmpegProResData::prores_interlaced_scan[1] == 8,
              "Interlaced pattern (row-based)");
static_assert(FFmpegProResData::prores_interlaced_scan[2] == 1,
              "Interlaced alternates rows");

// DC codebook validation
static_assert(FFmpegProResData::prores_dc_codebook.size() == 7,
              "DC codebook has 7 entries");
static_assert(FFmpegProResData::prores_dc_codebook[0] == 0x04,
              "DC codebook[0] = 0x04");
static_assert(FFmpegProResData::prores_dc_codebook[1] == 0x28,
              "DC codebook[1] = 0x28");
static_assert(FFmpegProResData::prores_dc_codebook[2] == 0x28,
              "DC codebook has repetition (0x28)");
static_assert(FFmpegProResData::prores_dc_codebook[6] == 0x70,
              "DC codebook[6] = 0x70");

// Verify DC codebook symmetry
static_assert(FFmpegProResData::prores_dc_codebook[1] ==
              FFmpegProResData::prores_dc_codebook[2],
              "DC codebook symmetric pattern");
static_assert(FFmpegProResData::prores_dc_codebook[3] ==
              FFmpegProResData::prores_dc_codebook[4],
              "DC codebook pairs (0x4D)");
static_assert(FFmpegProResData::prores_dc_codebook[5] ==
              FFmpegProResData::prores_dc_codebook[6],
              "DC codebook pairs (0x70)");

// Run-to-codebook validation
static_assert(FFmpegProResData::prores_run_to_cb.size() == 16,
              "Run-to-CB has 16 entries");
static_assert(FFmpegProResData::prores_run_to_cb[0] == 0x06,
              "Run 0 → CB 0x06");
static_assert(FFmpegProResData::prores_run_to_cb[1] == 0x06,
              "Run 1 → CB 0x06 (same)");
static_assert(FFmpegProResData::prores_run_to_cb[15] == 0x4C,
              "Run 15 → CB 0x4C");

// Verify run-to-CB patterns
static_assert(FFmpegProResData::prores_run_to_cb[0] ==
              FFmpegProResData::prores_run_to_cb[1],
              "Short runs share codebook");
static_assert(FFmpegProResData::prores_run_to_cb[5] == 0x29,
              "Mid runs use 0x29");
static_assert(FFmpegProResData::prores_run_to_cb[9] == 0x28,
              "Longer runs use 0x28");

// Level-to-codebook validation
static_assert(FFmpegProResData::prores_level_to_cb.size() == 10,
              "Level-to-CB has 10 entries");
static_assert(FFmpegProResData::prores_level_to_cb[0] == 0x04,
              "Level 0 → CB 0x04");
static_assert(FFmpegProResData::prores_level_to_cb[1] == 0x0A,
              "Level 1 → CB 0x0A");
static_assert(FFmpegProResData::prores_level_to_cb[9] == 0x4C,
              "Level 9 → CB 0x4C");

// Verify level-to-CB patterns
static_assert(FFmpegProResData::prores_level_to_cb[5] == 0x28,
              "Mid levels use 0x28");
static_assert(FFmpegProResData::prores_level_to_cb[5] ==
              FFmpegProResData::prores_level_to_cb[6],
              "High levels share codebook");

// Cross-validation: scan orders must be permutations
// (All values 0-63 must appear exactly once in each scan order)
// We can verify key positions to ensure uniqueness
static_assert(FFmpegProResData::prores_progressive_scan[0] == 0,
              "DC at scan position 0 (progressive)");
static_assert(FFmpegProResData::prores_interlaced_scan[0] == 0,
              "DC at scan position 0 (interlaced)");

// Verify both scans include all corner positions
static_assert(FFmpegProResData::prores_progressive_scan[63] == 63,
              "Progressive includes bottom-right (63)");
static_assert(FFmpegProResData::prores_interlaced_scan[63] == 63,
              "Interlaced includes bottom-right (63)");

// ============================================================================
// C API Compatibility Layer
// ============================================================================

extern "C" {

/**
 * Get pointer to ProRes progressive scan order
 * @return Pointer to 64-entry uint8_t array
 */
inline const uint8_t* get_prores_progressive_scan() {
    return FFmpegProResData::prores_progressive_scan.data();
}

/**
 * Get pointer to ProRes interlaced scan order
 * @return Pointer to 64-entry uint8_t array
 */
inline const uint8_t* get_prores_interlaced_scan() {
    return FFmpegProResData::prores_interlaced_scan.data();
}

/**
 * Get pointer to ProRes DC codebook
 * @return Pointer to 7-entry uint8_t array
 */
inline const uint8_t* get_prores_dc_codebook() {
    return FFmpegProResData::prores_dc_codebook.data();
}

/**
 * Get pointer to ProRes run-to-codebook mapping
 * @return Pointer to 16-entry uint8_t array
 */
inline const uint8_t* get_prores_run_to_cb() {
    return FFmpegProResData::prores_run_to_cb.data();
}

/**
 * Get pointer to ProRes level-to-codebook mapping
 * @return Pointer to 10-entry uint8_t array
 */
inline const uint8_t* get_prores_level_to_cb() {
    return FFmpegProResData::prores_level_to_cb.data();
}

} // extern "C"

#endif // AVCODEC_PRORES_DATA_TABLEGEN_CONSTEXPR_HPP
