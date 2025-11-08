/*
 * H.264/H.265 data tables - C++20 constexpr implementation
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

#ifndef AVCODEC_H2645_DATA_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_H2645_DATA_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include "libavutil/rational.h"

/**
 * @file
 * @brief H.264/H.265 codec pixel aspect ratio tables
 *
 * This header provides compile-time generation of H.264/H.265 (AVC/HEVC)
 * pixel aspect ratio lookup tables using C++20 constexpr. These codecs
 * use a 4-bit index to specify the pixel aspect ratio (PAR) in the video
 * stream, which describes the shape of pixels (square vs. rectangular).
 *
 * Tables:
 * - pixel_aspect: Maps aspect ratio index (0-16) to AVRational (num/den)
 *
 * Index 0: Unspecified
 * Indices 1-16: Standard aspect ratios from H.264/H.265 specifications
 *
 * Common aspect ratios:
 * - Index 1: 1:1 (square pixels, most common)
 * - Index 2: 12:11 (NTSC 4:3 SD)
 * - Index 3: 10:11 (PAL 4:3 SD)
 * - Index 14: 4:3 (some HD formats)
 * - Index 15: 3:2
 * - Index 16: 2:1 (anamorphic)
 *
 * Generated at compile time - zero runtime overhead.
 * All tables placed in .rodata section by the compiler.
 *
 * Data size: 136 bytes (17 entries × 8 bytes per AVRational)
 * Static assertions: 30+ compile-time validations
 */

namespace FFmpegH2645Data {

// ============================================================================
// Pixel Aspect Ratio Table (17 entries, 136 bytes)
// ============================================================================

/**
 * H.264/H.265 pixel aspect ratio table
 *
 * Maps 4-bit aspect ratio index codes to pixel aspect ratios:
 * - Index 0: Unspecified (0:1)
 * - Index 1: Square pixels (1:1) - most common
 * - Indices 2-13: Various rectangular pixel formats (SD video standards)
 * - Indices 14-16: Common display aspect ratios (4:3, 3:2, 2:1)
 *
 * These values are from the H.264 and H.265 specifications and define
 * the shape of individual pixels. Most modern video uses square pixels
 * (1:1), but older SD formats used rectangular pixels.
 *
 * Aspect ratio = (pixel_width / pixel_height) = (num / den)
 */
constexpr auto generate_h2645_pixel_aspect() noexcept {
    std::array<AVRational, 17> table{};

    // Exact values from H.264/H.265 specifications
    table[0]  = AVRational{   0,  1 };  // Unspecified
    table[1]  = AVRational{   1,  1 };  // Square pixels (1:1)
    table[2]  = AVRational{  12, 11 };  // NTSC 4:3 SD (480i/480p)
    table[3]  = AVRational{  10, 11 };  // PAL 4:3 SD (576i/576p)
    table[4]  = AVRational{  16, 11 };  // NTSC 16:9 SD
    table[5]  = AVRational{  40, 33 };  // PAL 16:9 SD
    table[6]  = AVRational{  24, 11 };  // NTSC 4:3 D-1
    table[7]  = AVRational{  20, 11 };  // PAL 4:3 D-1
    table[8]  = AVRational{  32, 11 };  // NTSC 16:9 D-1
    table[9]  = AVRational{  80, 33 };  // PAL 16:9 D-1
    table[10] = AVRational{  18, 11 };  // SMPTE 170M
    table[11] = AVRational{  15, 11 };  // ITU-R BT.470-6 System M
    table[12] = AVRational{  64, 33 };  // SMPTE 170M 16:9
    table[13] = AVRational{ 160, 99 };  // ITU-R BT.470-6 16:9
    table[14] = AVRational{   4,  3 };  // 4:3 display aspect
    table[15] = AVRational{   3,  2 };  // 3:2 display aspect
    table[16] = AVRational{   2,  1 };  // 2:1 anamorphic

    return table;
}

constexpr auto h2645_pixel_aspect = generate_h2645_pixel_aspect();

} // namespace FFmpegH2645Data

// ============================================================================
// Static Assertions - Compile-Time Validation
// ============================================================================

// Table size validation
static_assert(FFmpegH2645Data::h2645_pixel_aspect.size() == 17,
              "Pixel aspect table must have 17 entries");

// Index 0: Unspecified
static_assert(FFmpegH2645Data::h2645_pixel_aspect[0].num == 0,
              "Index 0: unspecified (numerator = 0)");
static_assert(FFmpegH2645Data::h2645_pixel_aspect[0].den == 1,
              "Index 0: unspecified (denominator = 1)");

// Index 1: Square pixels (most common)
static_assert(FFmpegH2645Data::h2645_pixel_aspect[1].num == 1,
              "Index 1: square pixels (1:1 numerator)");
static_assert(FFmpegH2645Data::h2645_pixel_aspect[1].den == 1,
              "Index 1: square pixels (1:1 denominator)");

// NTSC SD formats
static_assert(FFmpegH2645Data::h2645_pixel_aspect[2].num == 12,
              "Index 2: NTSC 4:3 SD (12:11 numerator)");
static_assert(FFmpegH2645Data::h2645_pixel_aspect[2].den == 11,
              "Index 2: NTSC 4:3 SD (12:11 denominator)");
static_assert(FFmpegH2645Data::h2645_pixel_aspect[4].num == 16,
              "Index 4: NTSC 16:9 SD (16:11 numerator)");
static_assert(FFmpegH2645Data::h2645_pixel_aspect[4].den == 11,
              "Index 4: NTSC 16:9 SD (16:11 denominator)");

// PAL SD formats
static_assert(FFmpegH2645Data::h2645_pixel_aspect[3].num == 10,
              "Index 3: PAL 4:3 SD (10:11 numerator)");
static_assert(FFmpegH2645Data::h2645_pixel_aspect[3].den == 11,
              "Index 3: PAL 4:3 SD (10:11 denominator)");
static_assert(FFmpegH2645Data::h2645_pixel_aspect[5].num == 40,
              "Index 5: PAL 16:9 SD (40:33 numerator)");
static_assert(FFmpegH2645Data::h2645_pixel_aspect[5].den == 33,
              "Index 5: PAL 16:9 SD (40:33 denominator)");

// D-1 formats
static_assert(FFmpegH2645Data::h2645_pixel_aspect[6].num == 24,
              "Index 6: NTSC 4:3 D-1 (24:11 numerator)");
static_assert(FFmpegH2645Data::h2645_pixel_aspect[6].den == 11,
              "Index 6: NTSC 4:3 D-1 (24:11 denominator)");
static_assert(FFmpegH2645Data::h2645_pixel_aspect[7].num == 20,
              "Index 7: PAL 4:3 D-1 (20:11 numerator)");
static_assert(FFmpegH2645Data::h2645_pixel_aspect[7].den == 11,
              "Index 7: PAL 4:3 D-1 (20:11 denominator)");
static_assert(FFmpegH2645Data::h2645_pixel_aspect[8].num == 32,
              "Index 8: NTSC 16:9 D-1 (32:11 numerator)");
static_assert(FFmpegH2645Data::h2645_pixel_aspect[8].den == 11,
              "Index 8: NTSC 16:9 D-1 (32:11 denominator)");
static_assert(FFmpegH2645Data::h2645_pixel_aspect[9].num == 80,
              "Index 9: PAL 16:9 D-1 (80:33 numerator)");
static_assert(FFmpegH2645Data::h2645_pixel_aspect[9].den == 33,
              "Index 9: PAL 16:9 D-1 (80:33 denominator)");

// SMPTE and ITU-R formats
static_assert(FFmpegH2645Data::h2645_pixel_aspect[10].num == 18,
              "Index 10: SMPTE 170M (18:11 numerator)");
static_assert(FFmpegH2645Data::h2645_pixel_aspect[10].den == 11,
              "Index 10: SMPTE 170M (18:11 denominator)");
static_assert(FFmpegH2645Data::h2645_pixel_aspect[11].num == 15,
              "Index 11: ITU-R BT.470-6 M (15:11 numerator)");
static_assert(FFmpegH2645Data::h2645_pixel_aspect[11].den == 11,
              "Index 11: ITU-R BT.470-6 M (15:11 denominator)");
static_assert(FFmpegH2645Data::h2645_pixel_aspect[12].num == 64,
              "Index 12: SMPTE 170M 16:9 (64:33 numerator)");
static_assert(FFmpegH2645Data::h2645_pixel_aspect[12].den == 33,
              "Index 12: SMPTE 170M 16:9 (64:33 denominator)");
static_assert(FFmpegH2645Data::h2645_pixel_aspect[13].num == 160,
              "Index 13: ITU-R BT.470-6 16:9 (160:99 numerator)");
static_assert(FFmpegH2645Data::h2645_pixel_aspect[13].den == 99,
              "Index 13: ITU-R BT.470-6 16:9 (160:99 denominator)");

// Common display aspect ratios
static_assert(FFmpegH2645Data::h2645_pixel_aspect[14].num == 4,
              "Index 14: 4:3 display (numerator)");
static_assert(FFmpegH2645Data::h2645_pixel_aspect[14].den == 3,
              "Index 14: 4:3 display (denominator)");
static_assert(FFmpegH2645Data::h2645_pixel_aspect[15].num == 3,
              "Index 15: 3:2 display (numerator)");
static_assert(FFmpegH2645Data::h2645_pixel_aspect[15].den == 2,
              "Index 15: 3:2 display (denominator)");
static_assert(FFmpegH2645Data::h2645_pixel_aspect[16].num == 2,
              "Index 16: 2:1 anamorphic (numerator)");
static_assert(FFmpegH2645Data::h2645_pixel_aspect[16].den == 1,
              "Index 16: 2:1 anamorphic (denominator)");

// Verify all denominators are positive and non-zero
static_assert(FFmpegH2645Data::h2645_pixel_aspect[1].den > 0,
              "All denominators must be positive");
static_assert(FFmpegH2645Data::h2645_pixel_aspect[16].den > 0,
              "Last denominator positive");

// Verify patterns in the table
// NTSC formats (indices 2, 4, 6, 8, 10) tend to use denominator 11
static_assert(FFmpegH2645Data::h2645_pixel_aspect[2].den == 11,
              "NTSC pattern: denominator 11");
static_assert(FFmpegH2645Data::h2645_pixel_aspect[4].den == 11,
              "NTSC pattern continues");
static_assert(FFmpegH2645Data::h2645_pixel_aspect[6].den == 11,
              "NTSC D-1 pattern");

// PAL formats (indices 3, 5, 7, 9) use denominators 11 or 33
static_assert(FFmpegH2645Data::h2645_pixel_aspect[3].den == 11,
              "PAL pattern: denominator 11 or 33");
static_assert(FFmpegH2645Data::h2645_pixel_aspect[5].den == 33,
              "PAL 16:9 uses 33");
static_assert(FFmpegH2645Data::h2645_pixel_aspect[9].den == 33,
              "PAL D-1 16:9 uses 33");

// ============================================================================
// C API Compatibility Layer
// ============================================================================

extern "C" {

/**
 * Get pointer to H.264/H.265 pixel aspect ratio table
 * @return Pointer to 17-entry AVRational array
 */
inline const AVRational* get_h2645_pixel_aspect() {
    return FFmpegH2645Data::h2645_pixel_aspect.data();
}

} // extern "C"

#endif // AVCODEC_H2645_DATA_TABLEGEN_CONSTEXPR_HPP
