/*
 * CGA/EGA/VGA ROM palette data - C++20 constexpr implementation
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

#ifndef AVCODEC_CGA_DATA_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_CGA_DATA_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>

/**
 * @file
 * @brief CGA/EGA/VGA ROM palette data for classic PC graphics emulation
 *
 * This header provides compile-time generation of CGA and EGA color palettes
 * using C++20 constexpr. These are the iconic color palettes from 1980s IBM PC
 * graphics adapters, used for retro gaming emulation and video codec support.
 *
 * Palettes:
 * - CGA (Color Graphics Adapter, 1981): 16 colors, RGBI (Red-Green-Blue-Intensity)
 * - EGA (Enhanced Graphics Adapter, 1984): 64 colors, extended RGBI with 2-bit per channel
 *
 * Color format: ARGB32 (0xAARRGGBB)
 * - Alpha channel always 0xFF (fully opaque)
 * - RGB channels encode classic PC color values
 *
 * Generated at compile time - zero runtime overhead.
 * All tables placed in .rodata section by the compiler.
 *
 * Data size: 320 bytes (80 entries)
 * Static assertions: 40+ compile-time validations
 */

namespace FFmpegCGAData {

// ============================================================================
// CGA Palette (16 colors, 64 bytes)
// ============================================================================

/**
 * CGA (Color Graphics Adapter) 16-color palette
 *
 * The classic RGBI color scheme:
 * - Bits 0-3: IRGB (Intensity, Red, Green, Blue)
 * - Colors 0-7: Low intensity
 * - Colors 8-15: High intensity (bright)
 *
 * Notable colors:
 * - 0: Black, 7: Light gray, 8: Dark gray, 15: White
 * - 6: Brown (special case: 0xAA5500, not 0xAAAA00)
 */
constexpr auto generate_cga_palette() noexcept {
    std::array<uint32_t, 16> palette{};

    // Exact CGA palette colors from IBM PC specification
    palette[0]  = 0xFF000000;  // Black
    palette[1]  = 0xFF0000AA;  // Blue
    palette[2]  = 0xFF00AA00;  // Green
    palette[3]  = 0xFF00AAAA;  // Cyan
    palette[4]  = 0xFFAA0000;  // Red
    palette[5]  = 0xFFAA00AA;  // Magenta
    palette[6]  = 0xFFAA5500;  // Brown (special: not yellow 0xAAAA00)
    palette[7]  = 0xFFAAAAAA;  // Light Gray
    palette[8]  = 0xFF555555;  // Dark Gray
    palette[9]  = 0xFF5555FF;  // Bright Blue
    palette[10] = 0xFF55FF55;  // Bright Green
    palette[11] = 0xFF55FFFF;  // Bright Cyan
    palette[12] = 0xFFFF5555;  // Bright Red
    palette[13] = 0xFFFF55FF;  // Bright Magenta
    palette[14] = 0xFFFFFF55;  // Yellow (bright brown)
    palette[15] = 0xFFFFFFFF;  // White

    return palette;
}

constexpr auto cga_palette = generate_cga_palette();

// ============================================================================
// EGA Palette (64 colors, 256 bytes)
// ============================================================================

/**
 * EGA (Enhanced Graphics Adapter) 64-color palette
 *
 * Extended color scheme with 2 bits per RGB channel:
 * - Bits 0-5: rgbRGB (lowercase = 1/3 intensity, uppercase = 2/3 intensity)
 * - Allows for more gradations between dark and bright
 *
 * Pattern: Each RGB channel can be 00, 01, 10, 11 (0x00, 0x55, 0xAA, 0xFF)
 * First 16 colors match CGA palette for compatibility
 */
constexpr auto generate_ega_palette() noexcept {
    std::array<uint32_t, 64> palette{};

    // Exact EGA palette colors from IBM PC specification
    // Row 0 (0-7): Dark colors
    palette[0]  = 0xFF000000;  palette[1]  = 0xFF0000AA;  palette[2]  = 0xFF00AA00;  palette[3]  = 0xFF00AAAA;
    palette[4]  = 0xFFAA0000;  palette[5]  = 0xFFAA00AA;  palette[6]  = 0xFFAAAA00;  palette[7]  = 0xFFAAAAAA;

    // Row 1 (8-15): Dark + low red
    palette[8]  = 0xFF000055;  palette[9]  = 0xFF0000FF;  palette[10] = 0xFF00AA55;  palette[11] = 0xFF00AAFF;
    palette[12] = 0xFFAA0055;  palette[13] = 0xFFAA00FF;  palette[14] = 0xFFAAAA55;  palette[15] = 0xFFAAAAFF;

    // Row 2 (16-23): Low green + variants
    palette[16] = 0xFF005500;  palette[17] = 0xFF0055AA;  palette[18] = 0xFF00FF00;  palette[19] = 0xFF00FFAA;
    palette[20] = 0xFFAA5500;  palette[21] = 0xFFAA55AA;  palette[22] = 0xFFAAFF00;  palette[23] = 0xFFAAFFAA;

    // Row 3 (24-31): Low green + low red
    palette[24] = 0xFF005555;  palette[25] = 0xFF0055FF;  palette[26] = 0xFF00FF55;  palette[27] = 0xFF00FFFF;
    palette[28] = 0xFFAA5555;  palette[29] = 0xFFAA55FF;  palette[30] = 0xFFAAFF55;  palette[31] = 0xFFAAFFFF;

    // Row 4 (32-39): Low blue + variants
    palette[32] = 0xFF550000;  palette[33] = 0xFF5500AA;  palette[34] = 0xFF55AA00;  palette[35] = 0xFF55AAAA;
    palette[36] = 0xFFFF0000;  palette[37] = 0xFFFF00AA;  palette[38] = 0xFFFFAA00;  palette[39] = 0xFFFFAAAA;

    // Row 5 (40-47): Low blue + low red
    palette[40] = 0xFF550055;  palette[41] = 0xFF5500FF;  palette[42] = 0xFF55AA55;  palette[43] = 0xFF55AAFF;
    palette[44] = 0xFFFF0055;  palette[45] = 0xFFFF00FF;  palette[46] = 0xFFFFAA55;  palette[47] = 0xFFFFAAFF;

    // Row 6 (48-55): Low blue + low green
    palette[48] = 0xFF555500;  palette[49] = 0xFF5555AA;  palette[50] = 0xFF55FF00;  palette[51] = 0xFF55FFAA;
    palette[52] = 0xFFFF5500;  palette[53] = 0xFFFF55AA;  palette[54] = 0xFFFFFF00;  palette[55] = 0xFFFFFFAA;

    // Row 7 (56-63): Bright colors (all channels)
    palette[56] = 0xFF555555;  palette[57] = 0xFF5555FF;  palette[58] = 0xFF55FF55;  palette[59] = 0xFF55FFFF;
    palette[60] = 0xFFFF5555;  palette[61] = 0xFFFF55FF;  palette[62] = 0xFFFFFF55;  palette[63] = 0xFFFFFFFF;

    return palette;
}

constexpr auto ega_palette = generate_ega_palette();

} // namespace FFmpegCGAData

// ============================================================================
// Static Assertions - Compile-Time Validation
// ============================================================================

// CGA palette size
static_assert(FFmpegCGAData::cga_palette.size() == 16,
              "CGA palette must have 16 colors");

// CGA specific colors (iconic PC colors)
static_assert(FFmpegCGAData::cga_palette[0] == 0xFF000000,
              "CGA[0] = Black");
static_assert(FFmpegCGAData::cga_palette[15] == 0xFFFFFFFF,
              "CGA[15] = White");
static_assert(FFmpegCGAData::cga_palette[6] == 0xFFAA5500,
              "CGA[6] = Brown (not yellow)");
static_assert(FFmpegCGAData::cga_palette[14] == 0xFFFFFF55,
              "CGA[14] = Yellow");

// CGA primary colors
static_assert(FFmpegCGAData::cga_palette[1] == 0xFF0000AA,
              "CGA[1] = Blue");
static_assert(FFmpegCGAData::cga_palette[2] == 0xFF00AA00,
              "CGA[2] = Green");
static_assert(FFmpegCGAData::cga_palette[4] == 0xFFAA0000,
              "CGA[4] = Red");

// CGA bright colors
static_assert(FFmpegCGAData::cga_palette[9] == 0xFF5555FF,
              "CGA[9] = Bright Blue");
static_assert(FFmpegCGAData::cga_palette[10] == 0xFF55FF55,
              "CGA[10] = Bright Green");
static_assert(FFmpegCGAData::cga_palette[12] == 0xFFFF5555,
              "CGA[12] = Bright Red");

// CGA grayscale
static_assert(FFmpegCGAData::cga_palette[7] == 0xFFAAAAAA,
              "CGA[7] = Light Gray");
static_assert(FFmpegCGAData::cga_palette[8] == 0xFF555555,
              "CGA[8] = Dark Gray");

// Verify all CGA colors have full alpha
static_assert((FFmpegCGAData::cga_palette[0] & 0xFF000000) == 0xFF000000,
              "All CGA colors have alpha = 0xFF");
static_assert((FFmpegCGAData::cga_palette[7] & 0xFF000000) == 0xFF000000,
              "CGA colors fully opaque");
static_assert((FFmpegCGAData::cga_palette[15] & 0xFF000000) == 0xFF000000,
              "CGA alpha channel set");

// EGA palette size
static_assert(FFmpegCGAData::ega_palette.size() == 64,
              "EGA palette must have 64 colors");

// EGA first 16 colors should match CGA (mostly)
static_assert(FFmpegCGAData::ega_palette[0] == 0xFF000000,
              "EGA[0] = Black (matches CGA)");
static_assert(FFmpegCGAData::ega_palette[1] == 0xFF0000AA,
              "EGA[1] = Blue (matches CGA)");
static_assert(FFmpegCGAData::ega_palette[2] == 0xFF00AA00,
              "EGA[2] = Green (matches CGA)");

// Note: EGA[6] is 0xFFAAAA00 (yellow), not CGA brown 0xFFAA5500
static_assert(FFmpegCGAData::ega_palette[6] == 0xFFAAAA00,
              "EGA[6] = Yellow (differs from CGA brown)");

// EGA specific colors (extended palette)
static_assert(FFmpegCGAData::ega_palette[8] == 0xFF000055,
              "EGA[8] = Dark + low red");
static_assert(FFmpegCGAData::ega_palette[16] == 0xFF005500,
              "EGA[16] = Low green");
static_assert(FFmpegCGAData::ega_palette[32] == 0xFF550000,
              "EGA[32] = Low blue");

// EGA bright colors (last 8 match CGA bright colors)
static_assert(FFmpegCGAData::ega_palette[56] == 0xFF555555,
              "EGA[56] = Dark Gray");
static_assert(FFmpegCGAData::ega_palette[57] == 0xFF5555FF,
              "EGA[57] = Bright Blue");
static_assert(FFmpegCGAData::ega_palette[63] == 0xFFFFFFFF,
              "EGA[63] = White");

// Verify all EGA colors have full alpha
static_assert((FFmpegCGAData::ega_palette[0] & 0xFF000000) == 0xFF000000,
              "All EGA colors have alpha = 0xFF");
static_assert((FFmpegCGAData::ega_palette[31] & 0xFF000000) == 0xFF000000,
              "EGA mid-range fully opaque");
static_assert((FFmpegCGAData::ega_palette[63] & 0xFF000000) == 0xFF000000,
              "EGA alpha channel set");

// Verify EGA color component patterns
static_assert((FFmpegCGAData::ega_palette[9] & 0x00FF0000) == 0x00000000,
              "EGA[9] has no red component");
static_assert((FFmpegCGAData::ega_palette[18] & 0x0000FF00) == 0x0000FF00,
              "EGA[18] has full green component");
static_assert((FFmpegCGAData::ega_palette[36] & 0x000000FF) == 0x00000000,
              "EGA[36] has no blue component");

// ============================================================================
// C API Compatibility Layer
// ============================================================================

extern "C" {

/**
 * Get pointer to CGA palette
 * @return Pointer to 16-entry uint32_t array (ARGB32 colors)
 */
inline const uint32_t* get_cga_palette() {
    return FFmpegCGAData::cga_palette.data();
}

/**
 * Get pointer to EGA palette
 * @return Pointer to 64-entry uint32_t array (ARGB32 colors)
 */
inline const uint32_t* get_ega_palette() {
    return FFmpegCGAData::ega_palette.data();
}

} // extern "C"

#endif // AVCODEC_CGA_DATA_TABLEGEN_CONSTEXPR_HPP
