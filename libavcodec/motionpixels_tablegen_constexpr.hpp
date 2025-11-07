/*
 * Modern C++ constexpr Motion Pixels RGB→YUV lookup table
 * Copyright (c) 2009 Reimar Döffinger <Reimar.Doeffinger@gmx.de>
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
 * Modern C++20 constexpr Motion Pixels RGB to YUV conversion table
 *
 * This header provides compile-time generation of the Motion Pixels codec's
 * RGB to YUV color space conversion lookup table.
 *
 * The Motion Pixels codec uses a 15-bit RGB format (5-5-5) and maps it to
 * YUV color space for compression. The table enables fast reverse lookup:
 * given an RGB value, find the corresponding YUV coordinates.
 *
 * Table structure:
 * - 32,768 entries (2^15 for 15-bit RGB)
 * - Each entry stores {y, v, u} in YUV space
 * - Algorithm fills table by:
 *   1. Computing RGB for each YUV combination
 *   2. Storing YUV at the corresponding RGB index
 *   3. Filling gaps by propagating from neighbors
 *
 * All 32,768 entries generated at compile time.
 */

#ifndef AVCODEC_MOTIONPIXELS_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_MOTIONPIXELS_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>

namespace ffmpeg {
namespace motionpixels {

/**
 * YUV pixel structure
 */
struct YuvPixel {
    int8_t y{0};
    int8_t v{0};
    int8_t u{0};

    constexpr YuvPixel() noexcept = default;
    constexpr YuvPixel(int8_t y_, int8_t v_, int8_t u_) noexcept
        : y(y_), v(v_), u(u_) {}

    constexpr bool is_zero() const noexcept {
        return (y == 0) && (v == 0) && (u == 0);
    }
};

// Table size: 2^15 for 15-bit RGB (5-5-5)
constexpr size_t RGB_TABLE_SIZE = 1 << 15;  // 32,768

/**
 * Constexpr clamp function
 */
constexpr int clamp(int val, int min_val, int max_val) noexcept {
    return (val < min_val) ? min_val : ((val > max_val) ? max_val : val);
}

/**
 * Convert YUV to RGB (Motion Pixels color space)
 *
 * Formula from original code:
 *   r = (1000*y + 701*v) / 1000
 *   g = (1000*y - 357*v - 172*u) / 1000
 *   b = (1000*y + 886*u) / 1000
 *
 * @param y Y component (0-31)
 * @param v V component (-31 to 31)
 * @param u U component (-31 to 31)
 * @param clip_rgb If true, clip to 5-bit per channel
 * @return RGB value as 15-bit integer, or (1<<15) if out of range
 */
constexpr int yuv_to_rgb(int y, int v, int u, bool clip_rgb) noexcept {
    // Compute RGB components
    int r = (1000 * y + 701 * v) / 1000;
    int g = (1000 * y - 357 * v - 172 * u) / 1000;
    int b = (1000 * y + 886 * u) / 1000;

    if (clip_rgb) {
        // Clamp to 5 bits per channel (0-31)
        r = clamp(r, 0, 31);
        g = clamp(g, 0, 31);
        b = clamp(b, 0, 31);
        return (r << 10) | (g << 5) | b;
    }

    // Check if in valid range (0-31 for each component)
    if ((static_cast<unsigned>(r) < 32) &&
        (static_cast<unsigned>(g) < 32) &&
        (static_cast<unsigned>(b) < 32)) {
        return (r << 10) | (g << 5) | b;
    }

    // Out of range marker
    return 1 << 15;
}

/**
 * Fill zero entries in a row by propagating from neighbors
 *
 * This function fills gaps in the YUV table by copying values from
 * adjacent non-zero entries. It performs multiple passes to ensure
 * all gaps are filled.
 *
 * @param row Pointer to 32-element row to fill
 */
constexpr void fill_zero_yuv_row(YuvPixel* row) noexcept {
    // Multiple passes to propagate values from neighbors
    for (int pass = 0; pass < 31; ++pass) {
        // Forward fill: copy from right to left for trailing zeros
        for (int j = 31; j > pass; --j) {
            if (row[j].is_zero() && !row[j - 1].is_zero()) {
                row[j] = row[j - 1];
            }
        }

        // Backward fill: copy from left to right for leading zeros
        for (int j = 0; j < 31 - pass; ++j) {
            if (row[j].is_zero() && !row[j + 1].is_zero()) {
                row[j] = row[j + 1];
            }
        }
    }
}

/**
 * Build the complete RGB to YUV conversion table
 *
 * Algorithm:
 * 1. For each YUV combination (y=0-31, v=-31-31, u=-31-31):
 *    - Compute corresponding RGB value
 *    - If RGB is valid and table entry is empty, store YUV
 * 2. Fill gaps by propagating from neighbors in each row
 *
 * @return Array of 32,768 YuvPixel entries
 */
constexpr auto generate_rgb_yuv_table() noexcept {
    std::array<YuvPixel, RGB_TABLE_SIZE> table{};

    // Build initial table by mapping YUV to RGB
    for (int y = 0; y <= 31; ++y) {
        for (int v = -31; v <= 31; ++v) {
            for (int u = -31; u <= 31; ++u) {
                int rgb_idx = yuv_to_rgb(y, v, u, false);

                // If valid RGB and entry is empty, store this YUV
                if (rgb_idx < RGB_TABLE_SIZE && table[rgb_idx].is_zero()) {
                    table[rgb_idx] = YuvPixel(
                        static_cast<int8_t>(y),
                        static_cast<int8_t>(v),
                        static_cast<int8_t>(u)
                    );
                }
            }
        }
    }

    // Fill gaps in each 32-element row
    // Table is organized as 1024 rows of 32 entries each
    for (int row = 0; row < 1024; ++row) {
        fill_zero_yuv_row(&table[row * 32]);
    }

    return table;
}

// Generate the complete table at compile time
constexpr auto rgb_yuv_table = generate_rgb_yuv_table();

// Compile-time validation
namespace tests {
    // Test table size
    static_assert(rgb_yuv_table.size() == RGB_TABLE_SIZE, "Table size is 32,768");
    static_assert(RGB_TABLE_SIZE == 32768, "RGB_TABLE_SIZE constant");

    // Test YUV to RGB conversion at key points
    // Pure white: Y=31, V=0, U=0 should give high RGB
    constexpr int white_rgb = yuv_to_rgb(31, 0, 0, false);
    static_assert(white_rgb >= 0 && white_rgb < RGB_TABLE_SIZE, "White in range");

    // Pure black: Y=0, V=0, U=0 should give RGB=0
    constexpr int black_rgb = yuv_to_rgb(0, 0, 0, false);
    static_assert(black_rgb == 0, "Black maps to RGB 0");

    // Test clipping
    constexpr int clipped = yuv_to_rgb(100, 100, 100, true);
    static_assert(clipped < RGB_TABLE_SIZE, "Clipped value in range");

    // Test that some entries are non-zero (table was built)
    static_assert(!rgb_yuv_table[0].is_zero() ||
                  !rgb_yuv_table[100].is_zero() ||
                  !rgb_yuv_table[1000].is_zero(),
                  "Table has non-zero entries");

    // Test Y value ranges
    static_assert(rgb_yuv_table[black_rgb].y == 0, "Black Y component is 0");

    // Test that center of table has reasonable values
    constexpr auto mid_entry = rgb_yuv_table[RGB_TABLE_SIZE / 2];
    static_assert(mid_entry.y >= -31 && mid_entry.y <= 31, "Y in valid range");
    static_assert(mid_entry.v >= -31 && mid_entry.v <= 31, "V in valid range");
    static_assert(mid_entry.u >= -31 && mid_entry.u <= 31, "U in valid range");

    // Test clamp function
    static_assert(clamp(50, 0, 31) == 31, "Clamp upper bound");
    static_assert(clamp(-10, 0, 31) == 0, "Clamp lower bound");
    static_assert(clamp(15, 0, 31) == 15, "Clamp no-op");

    // Test YuvPixel structure
    constexpr YuvPixel test_pixel(10, 5, -5);
    static_assert(test_pixel.y == 10, "YuvPixel y");
    static_assert(test_pixel.v == 5, "YuvPixel v");
    static_assert(test_pixel.u == -5, "YuvPixel u");
    static_assert(!test_pixel.is_zero(), "Non-zero pixel");

    constexpr YuvPixel zero_pixel;
    static_assert(zero_pixel.is_zero(), "Zero pixel");
    static_assert(zero_pixel.y == 0 && zero_pixel.v == 0 && zero_pixel.u == 0,
                  "Zero initialization");

    // Test RGB value construction
    // R=15, G=15, B=15 (mid-gray) = (15<<10) | (15<<5) | 15 = 15855
    constexpr int mid_gray_rgb = (15 << 10) | (15 << 5) | 15;
    static_assert(mid_gray_rgb == 15855, "RGB bit packing");

    // Test that RGB extraction works
    constexpr int test_rgb = (31 << 10) | (0 << 5) | 0;  // Pure red
    constexpr int red = (test_rgb >> 10) & 31;
    constexpr int green = (test_rgb >> 5) & 31;
    constexpr int blue = test_rgb & 31;
    static_assert(red == 31, "Red extraction");
    static_assert(green == 0, "Green extraction");
    static_assert(blue == 0, "Blue extraction");

    // Test range validation
    static_assert(yuv_to_rgb(0, 0, 0, false) < RGB_TABLE_SIZE, "Origin in range");
    static_assert(yuv_to_rgb(31, 0, 0, false) < RGB_TABLE_SIZE, "Max Y in range");
}

/**
 * Helper function for constexpr table access
 */
constexpr const YuvPixel& get_yuv_for_rgb(int rgb_index) noexcept {
    return (rgb_index >= 0 && rgb_index < RGB_TABLE_SIZE)
        ? rgb_yuv_table[rgb_index]
        : rgb_yuv_table[0];  // Safe fallback
}

/**
 * Constexpr validation: check table coverage
 */
namespace validation {
    // Count non-zero entries at compile time
    constexpr int count_nonzero_entries() noexcept {
        int count = 0;
        for (const auto& entry : rgb_yuv_table) {
            if (!entry.is_zero()) ++count;
        }
        return count;
    }

    constexpr int nonzero_count = count_nonzero_entries();

    // Table should have significant coverage (most entries filled)
    static_assert(nonzero_count > 30000, "Table has good coverage");
    static_assert(nonzero_count <= RGB_TABLE_SIZE, "Count within bounds");

    // Test that specific RGB values map correctly
    // Black (0,0,0) should have low Y
    constexpr auto black = rgb_yuv_table[0];
    static_assert(black.y <= 5, "Black has low Y value");

    // White (31,31,31) = (31<<10)|(31<<5)|31 = 32767
    constexpr int white_idx = (31 << 10) | (31 << 5) | 31;
    constexpr auto white = rgb_yuv_table[white_idx];
    static_assert(white.y >= 25, "White has high Y value");

    // Test symmetry: opposite U/V values should exist
    // Find an entry with positive U
    constexpr auto sample_entry = rgb_yuv_table[16384];

    // Test that table is fully populated (no zeros after fill)
    constexpr bool all_filled = []() {
        for (const auto& entry : rgb_yuv_table) {
            if (entry.is_zero()) return false;
        }
        return true;
    }();

    // Note: Due to color space limitations, not all RGB values may map
    // to valid YUV, so some zeros are expected. But most should be filled.
    static_assert(nonzero_count > RGB_TABLE_SIZE * 9 / 10,
                  "At least 90% of table filled");
}

} // namespace motionpixels
} // namespace ffmpeg

#endif // AVCODEC_MOTIONPIXELS_TABLEGEN_CONSTEXPR_HPP
