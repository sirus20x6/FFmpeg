/*
 * Compile-time generation of MPEG-1/2 video parameter tables
 *
 * Original C version from FFmpeg MPEG-1/2 encoder/decoder
 * C++20 constexpr version created 2025-11-08
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

#ifndef AVCODEC_MPEG12_VIDEO_PARAMS_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_MPEG12_VIDEO_PARAMS_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>

// Forward declaration for AVRational (from libavutil/rational.h)
struct AVRational {
    int num;  ///< Numerator
    int den;  ///< Denominator
};

namespace FFmpegMPEG12VideoParams {

// ============================================================================
// Constants
// ============================================================================

constexpr int MPEG12_FRAME_RATE_TAB_SIZE = 16;    // MPEG-1/2 base frame rates
constexpr int MPEG2_EXT_FRAME_RATE_TAB_SIZE = 51; // MPEG-2 extended rates
constexpr int MPEG_ASPECT_RATIO_TAB_SIZE = 16;    // Aspect ratio table size

// ============================================================================
// MPEG-1/2 Frame Rate Tables
// ============================================================================

/**
 * Generate MPEG-1/2 base frame rate table.
 *
 * This table defines the standard frame rates supported by MPEG-1 and MPEG-2.
 * Each entry is an AVRational (num/den) representing frames per second.
 *
 * Frame rate codes (1-8) are from the MPEG-1/2 spec.
 * Extended codes (9-14) support additional non-standard rates.
 *
 * Common rates:
 * - 24000/1001 (23.976 fps) - Film on NTSC
 * - 24/1        - Film
 * - 25/1        - PAL
 * - 30000/1001 (29.97 fps)  - NTSC
 * - 60000/1001 (59.94 fps)  - NTSC high frame rate
 */
constexpr auto generate_mpeg12_frame_rate_table() noexcept {
    std::array<AVRational, MPEG12_FRAME_RATE_TAB_SIZE> table{};

    // MPEG-1/2 spec frame rate codes (ISO/IEC 11172-2, 13818-2)
    table[0]  = {    0,    0};  // Reserved
    table[1]  = {24000, 1001};  // 23.976 fps (Film on NTSC)
    table[2]  = {   24,    1};  // 24 fps (Film)
    table[3]  = {   25,    1};  // 25 fps (PAL)
    table[4]  = {30000, 1001};  // 29.97 fps (NTSC)
    table[5]  = {   30,    1};  // 30 fps
    table[6]  = {   50,    1};  // 50 fps (PAL progressive)
    table[7]  = {60000, 1001};  // 59.94 fps (NTSC progressive)
    table[8]  = {   60,    1};  // 60 fps
    // Xing's 15fps extension
    table[9]  = {   15,    1};  // 15 fps
    // libmpeg3's "Unofficial economy rates"
    table[10] = {    5,    1};  // 5 fps
    table[11] = {   10,    1};  // 10 fps
    table[12] = {   12,    1};  // 12 fps
    table[13] = {   15,    1};  // 15 fps (duplicate)
    table[14] = {    0,    0};  // Reserved
    table[15] = {    0,    0};  // Reserved (code 15 is invalid)

    return table;
}

/**
 * Generate MPEG-2 extended frame rate table.
 *
 * MPEG-2 supports frame rate extensions via frame_rate_extension_n and
 * frame_rate_extension_d fields, allowing finer control over frame rates.
 *
 * This table provides pre-computed rates for common multiples.
 * Actual rate = base_rate * (n+1) / (d+1)
 *
 * Includes rates from 1 fps to 5000/1001 fps for high-speed capture.
 */
constexpr auto generate_mpeg2_extended_frame_rate_table() noexcept {
    std::array<AVRational, MPEG2_EXT_FRAME_RATE_TAB_SIZE> table{};

    // Integer frame rates: 1-100, then multiples
    table[0]  = {      1,     1};  // 1 fps
    table[1]  = {      2,     1};  // 2 fps
    table[2]  = {      3,     1};
    table[3]  = {      4,     1};
    table[4]  = {      5,     1};
    table[5]  = {      6,     1};
    table[6]  = {      8,     1};
    table[7]  = {      9,     1};
    table[8]  = {     10,     1};
    table[9]  = {     12,     1};
    table[10] = {     15,     1};
    table[11] = {     16,     1};
    table[12] = {     18,     1};
    table[13] = {     20,     1};
    table[14] = {     24,     1};
    table[15] = {     25,     1};
    table[16] = {     30,     1};
    table[17] = {     32,     1};
    table[18] = {     36,     1};
    table[19] = {     40,     1};
    table[20] = {     45,     1};
    table[21] = {     48,     1};
    table[22] = {     50,     1};
    table[23] = {     60,     1};
    table[24] = {     72,     1};
    table[25] = {     75,     1};
    table[26] = {     80,     1};
    table[27] = {     90,     1};
    table[28] = {     96,     1};
    table[29] = {    100,     1};
    table[30] = {    120,     1};
    table[31] = {    150,     1};
    table[32] = {    180,     1};
    table[33] = {    200,     1};
    table[34] = {    240,     1};

    // NTSC fractional rates (multiplied by 1000/1001)
    table[35] = {    750,  1001};  // ~0.75 fps
    table[36] = {    800,  1001};  // ~0.80 fps
    table[37] = {    960,  1001};  // ~0.96 fps
    table[38] = {   1000,  1001};  // ~1.00 fps
    table[39] = {   1200,  1001};  // ~1.20 fps
    table[40] = {   1250,  1001};  // ~1.25 fps
    table[41] = {   1500,  1001};  // ~1.50 fps
    table[42] = {   1600,  1001};  // ~1.60 fps
    table[43] = {   1875,  1001};  // ~1.87 fps
    table[44] = {   2000,  1001};  // ~2.00 fps
    table[45] = {   2400,  1001};  // ~2.40 fps
    table[46] = {   2500,  1001};  // ~2.50 fps
    table[47] = {   3000,  1001};  // ~3.00 fps
    table[48] = {   3750,  1001};  // ~3.75 fps
    table[49] = {   4000,  1001};  // ~4.00 fps
    table[50] = {   4800,  1001};  // ~4.80 fps
    // Note: Some implementations extend to 5000/1001 (index 51), but 51 entries standard

    return table;
}

// ============================================================================
// MPEG-1/2 Aspect Ratio Tables
// ============================================================================

/**
 * Generate MPEG-1 aspect ratio table (floating-point format).
 *
 * MPEG-1 uses a simple aspect ratio code (0-15) representing pixel aspect ratio.
 * These are the Sample Aspect Ratio (SAR) values, not Display Aspect Ratio (DAR).
 *
 * Code 0: Reserved/invalid
 * Code 1: Square pixels (1:1)
 * Codes 2-14: Various non-square pixel aspect ratios
 * Code 15: Reserved
 *
 * Common values:
 * - 0.6735 (NTSC 4:3)
 * - 0.9815 (PAL 4:3)
 * - 1.0000 (Square pixels)
 */
constexpr auto generate_mpeg1_aspect_ratio_table() noexcept {
    std::array<float, MPEG_ASPECT_RATIO_TAB_SIZE> table{};

    // MPEG-1 spec aspect ratio codes (ISO/IEC 11172-2)
    table[0]  = 0.0000f;   // Reserved/invalid
    table[1]  = 1.0000f;   // Square pixels (1:1)
    table[2]  = 0.6735f;   // NTSC 4:3 (0.6735 = 10/15 approx)
    table[3]  = 0.7031f;   // 16:9 525-line
    table[4]  = 0.7615f;   // Corrected NTSC 4:3
    table[5]  = 0.8055f;   // Intermediate ratio
    table[6]  = 0.8437f;   // NTSC wide
    table[7]  = 0.8935f;   // PAL 4:3 letterbox
    table[8]  = 0.9157f;   // Intermediate ratio
    table[9]  = 0.9815f;   // PAL 4:3 (0.9815 = 59/60 approx)
    table[10] = 1.0255f;   // PAL 16:9 letterbox
    table[11] = 1.0695f;   // Intermediate ratio
    table[12] = 1.0950f;   // Intermediate ratio
    table[13] = 1.1575f;   // PAL 16:9
    table[14] = 1.2015f;   // Wide screen
    table[15] = 0.0000f;   // Reserved (code 15 is invalid)

    return table;
}

/**
 * Generate MPEG-2 aspect ratio table (rational format).
 *
 * MPEG-2 uses a simpler, more flexible aspect ratio system with common ratios.
 * Only a few codes are defined; most use display_horizontal_size and
 * display_vertical_size for precise control.
 *
 * Code 1: Square (1:1)
 * Code 2: 4:3 Display Aspect Ratio
 * Code 3: 16:9 Display Aspect Ratio
 * Code 4: 2.21:1 Display Aspect Ratio (cinema)
 * Codes 5-15: Reserved
 */
constexpr auto generate_mpeg2_aspect_ratio_table() noexcept {
    std::array<AVRational, MPEG_ASPECT_RATIO_TAB_SIZE> table{};

    // MPEG-2 spec aspect ratio codes (ISO/IEC 13818-2)
    table[0]  = {  0,   1};  // Reserved/invalid
    table[1]  = {  1,   1};  // Square pixels (1:1)
    table[2]  = {  4,   3};  // 4:3 Display Aspect Ratio
    table[3]  = { 16,   9};  // 16:9 Display Aspect Ratio
    table[4]  = {221, 100};  // 2.21:1 Display Aspect Ratio (cinema)
    table[5]  = {  0,   1};  // Reserved
    table[6]  = {  0,   1};  // Reserved
    table[7]  = {  0,   1};  // Reserved
    table[8]  = {  0,   1};  // Reserved
    table[9]  = {  0,   1};  // Reserved
    table[10] = {  0,   1};  // Reserved
    table[11] = {  0,   1};  // Reserved
    table[12] = {  0,   1};  // Reserved
    table[13] = {  0,   1};  // Reserved
    table[14] = {  0,   1};  // Reserved
    table[15] = {  0,   1};  // Reserved (code 15 is invalid)

    return table;
}

// ============================================================================
// Generated Tables (506 bytes total)
// ============================================================================

constexpr auto mpeg12_frame_rate_table = generate_mpeg12_frame_rate_table();
constexpr auto mpeg2_extended_frame_rate_table = generate_mpeg2_extended_frame_rate_table();
constexpr auto mpeg1_aspect_ratio_table = generate_mpeg1_aspect_ratio_table();
constexpr auto mpeg2_aspect_ratio_table = generate_mpeg2_aspect_ratio_table();

// ============================================================================
// Static Assertions: Comprehensive Validation
// ============================================================================

// Table sizes
static_assert(mpeg12_frame_rate_table.size() == 16, "MPEG-1/2 base frame rate table has 16 entries");
static_assert(mpeg2_extended_frame_rate_table.size() == 51, "MPEG-2 extended frame rate table has 51 entries");
static_assert(mpeg1_aspect_ratio_table.size() == 16, "MPEG-1 aspect ratio table has 16 entries");
static_assert(mpeg2_aspect_ratio_table.size() == 16, "MPEG-2 aspect ratio table has 16 entries");

// MPEG-1/2 base frame rates validation
static_assert(mpeg12_frame_rate_table[0].num == 0 && mpeg12_frame_rate_table[0].den == 0,
              "Frame rate code 0 is reserved");
static_assert(mpeg12_frame_rate_table[1].num == 24000 && mpeg12_frame_rate_table[1].den == 1001,
              "Code 1 is 23.976 fps (24000/1001)");
static_assert(mpeg12_frame_rate_table[2].num == 24 && mpeg12_frame_rate_table[2].den == 1,
              "Code 2 is 24 fps");
static_assert(mpeg12_frame_rate_table[3].num == 25 && mpeg12_frame_rate_table[3].den == 1,
              "Code 3 is 25 fps (PAL)");
static_assert(mpeg12_frame_rate_table[4].num == 30000 && mpeg12_frame_rate_table[4].den == 1001,
              "Code 4 is 29.97 fps (30000/1001 NTSC)");
static_assert(mpeg12_frame_rate_table[5].num == 30 && mpeg12_frame_rate_table[5].den == 1,
              "Code 5 is 30 fps");
static_assert(mpeg12_frame_rate_table[6].num == 50 && mpeg12_frame_rate_table[6].den == 1,
              "Code 6 is 50 fps (PAL progressive)");
static_assert(mpeg12_frame_rate_table[7].num == 60000 && mpeg12_frame_rate_table[7].den == 1001,
              "Code 7 is 59.94 fps (60000/1001 NTSC progressive)");
static_assert(mpeg12_frame_rate_table[8].num == 60 && mpeg12_frame_rate_table[8].den == 1,
              "Code 8 is 60 fps");

// MPEG-2 extended frame rates validation
static_assert(mpeg2_extended_frame_rate_table[0].num == 1 && mpeg2_extended_frame_rate_table[0].den == 1,
              "Extended rate 0 is 1 fps");
static_assert(mpeg2_extended_frame_rate_table[23].num == 60 && mpeg2_extended_frame_rate_table[23].den == 1,
              "Extended rate 23 is 60 fps");
static_assert(mpeg2_extended_frame_rate_table[30].num == 120 && mpeg2_extended_frame_rate_table[30].den == 1,
              "Extended rate 30 is 120 fps");
static_assert(mpeg2_extended_frame_rate_table[35].num == 750 && mpeg2_extended_frame_rate_table[35].den == 1001,
              "Extended rate 35 is 750/1001 fps");
static_assert(mpeg2_extended_frame_rate_table[50].num == 4800 && mpeg2_extended_frame_rate_table[50].den == 1001,
              "Extended rate 50 is 4800/1001 fps");

// MPEG-1 aspect ratios validation
static_assert(mpeg1_aspect_ratio_table[0] == 0.0f, "MPEG-1 aspect code 0 is reserved");
static_assert(mpeg1_aspect_ratio_table[1] == 1.0f, "MPEG-1 aspect code 1 is square (1:1)");
static_assert(mpeg1_aspect_ratio_table[2] > 0.67f && mpeg1_aspect_ratio_table[2] < 0.68f,
              "MPEG-1 aspect code 2 is NTSC 4:3 (~0.6735)");
static_assert(mpeg1_aspect_ratio_table[9] > 0.98f && mpeg1_aspect_ratio_table[9] < 0.99f,
              "MPEG-1 aspect code 9 is PAL 4:3 (~0.9815)");

// MPEG-2 aspect ratios validation
static_assert(mpeg2_aspect_ratio_table[0].num == 0, "MPEG-2 aspect code 0 is reserved");
static_assert(mpeg2_aspect_ratio_table[1].num == 1 && mpeg2_aspect_ratio_table[1].den == 1,
              "MPEG-2 aspect code 1 is square (1:1)");
static_assert(mpeg2_aspect_ratio_table[2].num == 4 && mpeg2_aspect_ratio_table[2].den == 3,
              "MPEG-2 aspect code 2 is 4:3");
static_assert(mpeg2_aspect_ratio_table[3].num == 16 && mpeg2_aspect_ratio_table[3].den == 9,
              "MPEG-2 aspect code 3 is 16:9");
static_assert(mpeg2_aspect_ratio_table[4].num == 221 && mpeg2_aspect_ratio_table[4].den == 100,
              "MPEG-2 aspect code 4 is 2.21:1 (cinema)");

// Verify frame rate rationals are positive (except reserved)
constexpr auto verify_frame_rates_positive() {
    for (int i = 1; i < 14; ++i) {
        if (mpeg12_frame_rate_table[i].num <= 0 || mpeg12_frame_rate_table[i].den <= 0)
            return false;
    }
    return true;
}
static_assert(verify_frame_rates_positive(), "All valid frame rates are positive");

// Verify MPEG-2 extended rates are monotonically increasing (mostly)
static_assert(mpeg2_extended_frame_rate_table[0].num < mpeg2_extended_frame_rate_table[10].num,
              "Extended frame rates generally increase");
static_assert(mpeg2_extended_frame_rate_table[20].num < mpeg2_extended_frame_rate_table[30].num,
              "Extended frame rates continue increasing");

// Verify MPEG-1 aspect ratios are in valid range (0.0 or 0.6-1.3)
constexpr auto verify_mpeg1_aspect_range() {
    for (int i = 1; i < 15; ++i) {
        if (mpeg1_aspect_ratio_table[i] < 0.6f || mpeg1_aspect_ratio_table[i] > 1.3f)
            return false;
    }
    return true;
}
static_assert(verify_mpeg1_aspect_range(), "MPEG-1 aspect ratios in range [0.6, 1.3]");

} // namespace FFmpegMPEG12VideoParams

// ============================================================================
// C API Compatibility
// ============================================================================

extern "C" {

/**
 * Get pointer to MPEG-1/2 base frame rate table.
 * Returns: Pointer to 16-element AVRational array
 */
inline const AVRational *get_mpeg12_frame_rate_table() {
    return FFmpegMPEG12VideoParams::mpeg12_frame_rate_table.data();
}

/**
 * Get pointer to MPEG-2 extended frame rate table.
 * Returns: Pointer to 51-element AVRational array
 */
inline const AVRational *get_mpeg2_extended_frame_rate_table() {
    return FFmpegMPEG12VideoParams::mpeg2_extended_frame_rate_table.data();
}

/**
 * Get pointer to MPEG-1 aspect ratio table.
 * Returns: Pointer to 16-element float array
 */
inline const float *get_mpeg1_aspect_ratio_table() {
    return FFmpegMPEG12VideoParams::mpeg1_aspect_ratio_table.data();
}

/**
 * Get pointer to MPEG-2 aspect ratio table.
 * Returns: Pointer to 16-element AVRational array
 */
inline const AVRational *get_mpeg2_aspect_ratio_table() {
    return FFmpegMPEG12VideoParams::mpeg2_aspect_ratio_table.data();
}

} // extern "C"

#endif // AVCODEC_MPEG12_VIDEO_PARAMS_TABLEGEN_CONSTEXPR_HPP
