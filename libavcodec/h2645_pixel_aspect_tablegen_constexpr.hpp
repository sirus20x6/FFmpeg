/*
 * Compile-time generation of H.264/H.265 pixel aspect ratio table
 *
 * Original C version from FFmpeg H.264/H.265 decoder
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

#ifndef AVCODEC_H2645_PIXEL_ASPECT_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_H2645_PIXEL_ASPECT_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>

// Forward declaration for AVRational (from libavutil/rational.h)
struct AVRational {
    int num;  ///< Numerator
    int den;  ///< Denominator
};

namespace FFmpegH2645PixelAspect {

// ============================================================================
// Constants
// ============================================================================

constexpr int H2645_PIXEL_ASPECT_TABLE_SIZE = 17;  // Aspect ratio codes 0-16

// ============================================================================
// H.264/H.265 Pixel Aspect Ratio Table
// ============================================================================

/**
 * Generate H.264/H.265 pixel aspect ratio table.
 *
 * This table defines standard Sample Aspect Ratio (SAR) values used by both
 * H.264 (AVC) and H.265 (HEVC) video coding standards. The aspect_ratio_idc
 * field in the VUI (Video Usability Information) parameters indicates which
 * standard ratio to use.
 *
 * SAR describes the shape of a pixel (width:height ratio), which combined
 * with the picture dimensions gives the Display Aspect Ratio (DAR).
 *
 * Common codes:
 * - 0:  Unspecified (application decides)
 * - 1:  Square pixels (1:1) - most modern displays
 * - 2:  12:11 - NTSC 4:3 (525-line)
 * - 3:  10:11 - NTSC wide
 * - 4:  16:11 - NTSC 16:9
 * - 8:  32:11 - PAL 16:9 (625-line)
 * - 13: 160:99 - Extended PAL 16:9
 * - 14: 4:3 - Direct DAR
 * - 15: 3:2 - Direct DAR
 * - 16: 2:1 - Cinema wide
 *
 * If aspect_ratio_idc == 255 (Extended_SAR), custom values follow in bitstream.
 */
constexpr auto generate_h2645_pixel_aspect_table() noexcept {
    std::array<AVRational, H2645_PIXEL_ASPECT_TABLE_SIZE> table{};

    // H.264/H.265 spec: Table E-1 (Interpretation of sample aspect ratio indicator)
    table[0]  = {   0,  1 };  // 0: Unspecified
    table[1]  = {   1,  1 };  // 1: Square pixels (1:1)
    table[2]  = {  12, 11 };  // 2: 12:11 (NTSC 4:3, 525-line)
    table[3]  = {  10, 11 };  // 3: 10:11 (NTSC narrow)
    table[4]  = {  16, 11 };  // 4: 16:11 (NTSC 16:9)
    table[5]  = {  40, 33 };  // 5: 40:33 (PAL 4:3, 625-line)
    table[6]  = {  24, 11 };  // 6: 24:11 (NTSC wide 16:9)
    table[7]  = {  20, 11 };  // 7: 20:11
    table[8]  = {  32, 11 };  // 8: 32:11 (PAL 16:9)
    table[9]  = {  80, 33 };  // 9: 80:33 (PAL wide 16:9)
    table[10] = {  18, 11 };  // 10: 18:11
    table[11] = {  15, 11 };  // 11: 15:11
    table[12] = {  64, 33 };  // 12: 64:33
    table[13] = { 160, 99 };  // 13: 160:99 (Extended PAL 16:9)
    table[14] = {   4,  3 };  // 14: 4:3 (Direct DAR)
    table[15] = {   3,  2 };  // 15: 3:2 (Direct DAR)
    table[16] = {   2,  1 };  // 16: 2:1 (Cinema wide)

    return table;
}

// ============================================================================
// Generated Table (136 bytes total)
// ============================================================================

constexpr auto h2645_pixel_aspect_table = generate_h2645_pixel_aspect_table();

// ============================================================================
// Static Assertions: Comprehensive Validation
// ============================================================================

// Table size
static_assert(h2645_pixel_aspect_table.size() == 17, "H.2645 pixel aspect table has 17 entries");

// Code 0: Unspecified
static_assert(h2645_pixel_aspect_table[0].num == 0, "Code 0 is unspecified (0:1)");
static_assert(h2645_pixel_aspect_table[0].den == 1, "Code 0 denominator is 1");

// Code 1: Square pixels (most common)
static_assert(h2645_pixel_aspect_table[1].num == 1, "Code 1 is square pixels (1:1)");
static_assert(h2645_pixel_aspect_table[1].den == 1, "Code 1 is square pixels (1:1)");

// NTSC ratios (codes 2-4, 6-7, 10-11)
static_assert(h2645_pixel_aspect_table[2].num == 12 && h2645_pixel_aspect_table[2].den == 11,
              "Code 2 is NTSC 4:3 (12:11)");
static_assert(h2645_pixel_aspect_table[3].num == 10 && h2645_pixel_aspect_table[3].den == 11,
              "Code 3 is NTSC narrow (10:11)");
static_assert(h2645_pixel_aspect_table[4].num == 16 && h2645_pixel_aspect_table[4].den == 11,
              "Code 4 is NTSC 16:9 (16:11)");

// PAL ratios (codes 5, 8-9, 12-13)
static_assert(h2645_pixel_aspect_table[5].num == 40 && h2645_pixel_aspect_table[5].den == 33,
              "Code 5 is PAL 4:3 (40:33)");
static_assert(h2645_pixel_aspect_table[8].num == 32 && h2645_pixel_aspect_table[8].den == 11,
              "Code 8 is PAL 16:9 (32:11)");
static_assert(h2645_pixel_aspect_table[9].num == 80 && h2645_pixel_aspect_table[9].den == 33,
              "Code 9 is PAL wide 16:9 (80:33)");
static_assert(h2645_pixel_aspect_table[13].num == 160 && h2645_pixel_aspect_table[13].den == 99,
              "Code 13 is extended PAL 16:9 (160:99)");

// Direct DAR codes (14-16)
static_assert(h2645_pixel_aspect_table[14].num == 4 && h2645_pixel_aspect_table[14].den == 3,
              "Code 14 is direct 4:3 DAR");
static_assert(h2645_pixel_aspect_table[15].num == 3 && h2645_pixel_aspect_table[15].den == 2,
              "Code 15 is direct 3:2 DAR");
static_assert(h2645_pixel_aspect_table[16].num == 2 && h2645_pixel_aspect_table[16].den == 1,
              "Code 16 is cinema wide (2:1)");

// Verify all valid entries have positive denominators
constexpr auto verify_positive_denominators() {
    for (int i = 1; i < H2645_PIXEL_ASPECT_TABLE_SIZE; ++i) {
        if (h2645_pixel_aspect_table[i].den <= 0) return false;
    }
    return true;
}
static_assert(verify_positive_denominators(), "All valid entries have positive denominators");

// Verify all valid entries have non-negative numerators
constexpr auto verify_nonnegative_numerators() {
    for (int i = 0; i < H2645_PIXEL_ASPECT_TABLE_SIZE; ++i) {
        if (h2645_pixel_aspect_table[i].num < 0) return false;
    }
    return true;
}
static_assert(verify_nonnegative_numerators(), "All entries have non-negative numerators");

// Verify square pixels and wider-than-square ratios
static_assert(h2645_pixel_aspect_table[1].num == h2645_pixel_aspect_table[1].den,
              "Code 1 is exactly square");
static_assert(h2645_pixel_aspect_table[16].num > h2645_pixel_aspect_table[16].den,
              "Code 16 (2:1) is wider than square");

// Verify common NTSC property: numerator/denominator close to standard values
static_assert(h2645_pixel_aspect_table[2].num * 11 == 12 * h2645_pixel_aspect_table[2].den,
              "Code 2 simplifies to 12:11");
static_assert(h2645_pixel_aspect_table[4].num * 11 == 16 * h2645_pixel_aspect_table[4].den,
              "Code 4 simplifies to 16:11");

// Verify common PAL property
static_assert(h2645_pixel_aspect_table[5].num * 33 == 40 * h2645_pixel_aspect_table[5].den,
              "Code 5 simplifies to 40:33");

} // namespace FFmpegH2645PixelAspect

// ============================================================================
// C API Compatibility
// ============================================================================

extern "C" {

/**
 * Get pointer to H.264/H.265 pixel aspect ratio table.
 * Returns: Pointer to 17-element AVRational array
 */
inline const AVRational *get_h2645_pixel_aspect_table() {
    return FFmpegH2645PixelAspect::h2645_pixel_aspect_table.data();
}

} // extern "C"

#endif // AVCODEC_H2645_PIXEL_ASPECT_TABLEGEN_CONSTEXPR_HPP
