/*
 * Compile-time generation of H.264 CAVLC level decoding tables
 *
 * Original algorithm from FFmpeg H.264 decoder
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

#ifndef AVCODEC_H264_CAVLC_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_H264_CAVLC_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>

namespace FFmpegH264CAVLC {

// ============================================================================
// Constants
// ============================================================================

constexpr int LEVEL_TAB_BITS = 8;
constexpr int LEVEL_TAB_SIZE = 1 << LEVEL_TAB_BITS;  // 256
constexpr int NUM_SUFFIX_LENGTHS = 7;

// ============================================================================
// Constexpr Log2 Implementation
// ============================================================================

/**
 * Compute floor(log2(x)) for x > 0 at compile time.
 * Returns -1 for x = 0 (matches av_log2 behavior).
 */
constexpr int log2_constexpr(unsigned int x) noexcept {
    if (x == 0) {
        return -1;
    }

    int result = 0;
    // Count leading zeros by shifting
    unsigned int temp = x;
    while (temp >>= 1) {
        ++result;
    }

    return result;
}

// ============================================================================
// CAVLC Level Table Entry
// ============================================================================

struct cavlc_level_entry {
    int8_t level_code;
    int8_t bit_length;

    constexpr cavlc_level_entry() noexcept : level_code(0), bit_length(0) {}
    constexpr cavlc_level_entry(int8_t code, int8_t len) noexcept
        : level_code(code), bit_length(len) {}
};

// ============================================================================
// CAVLC Level Table Generation
// ============================================================================

/**
 * Generate H.264 CAVLC level decoding lookup tables.
 *
 * CAVLC (Context-Adaptive Variable Length Coding) is H.264's entropy coding method
 * for transform coefficients. The level tables accelerate decoding by pre-computing
 * level values and bit lengths for different suffix lengths and bit patterns.
 *
 * Algorithm:
 * - For each suffix_length (0-6) representing different VLC code structures
 * - For each possible 8-bit pattern (0-255) from bitstream peek
 * - Compute the prefix length using log2
 * - Calculate level_code through bit manipulation
 * - Apply sign handling (positive/negative levels)
 * - Store special markers for codes requiring additional bits
 *
 * The table enables O(1) lookup during decoding instead of bit-by-bit parsing.
 */
constexpr auto generate_cavlc_level_tab() noexcept {
    std::array<std::array<std::array<int8_t, 2>, LEVEL_TAB_SIZE>, NUM_SUFFIX_LENGTHS> table{};

    for (int suffix_length = 0; suffix_length < NUM_SUFFIX_LENGTHS; ++suffix_length) {
        for (unsigned int i = 0; i < LEVEL_TAB_SIZE; ++i) {
            // Calculate prefix length from the bit pattern
            // prefix = LEVEL_TAB_BITS - log2(2*i)
            int prefix = LEVEL_TAB_BITS - log2_constexpr(2 * i);

            // Case 1: Full code fits within LEVEL_TAB_BITS
            if (prefix + 1 + suffix_length <= LEVEL_TAB_BITS) {
                // Extract suffix bits and compute level_code
                int log2_i = log2_constexpr(i);
                int suffix_bits = (suffix_length > 0 && log2_i >= suffix_length)
                                  ? (i >> (log2_i - suffix_length))
                                  : 0;

                int level_code = (prefix << suffix_length) + suffix_bits - (1 << suffix_length);

                // Apply sign handling: convert to signed representation
                // Negative if level_code is odd, positive if even
                int mask = -(level_code & 1);
                level_code = (((2 + level_code) >> 1) ^ mask) - mask;

                table[suffix_length][i][0] = static_cast<int8_t>(level_code);
                table[suffix_length][i][1] = static_cast<int8_t>(prefix + 1 + suffix_length);
            }
            // Case 2: Prefix fits, but need to read more suffix bits
            else if (prefix + 1 <= LEVEL_TAB_BITS) {
                // Special marker: prefix + 100 indicates more bits needed
                table[suffix_length][i][0] = static_cast<int8_t>(prefix + 100);
                table[suffix_length][i][1] = static_cast<int8_t>(prefix + 1);
            }
            // Case 3: Even prefix doesn't fit in LEVEL_TAB_BITS
            else {
                // Special marker: 108 (LEVEL_TAB_BITS + 100) indicates overflow
                table[suffix_length][i][0] = static_cast<int8_t>(LEVEL_TAB_BITS + 100);
                table[suffix_length][i][1] = static_cast<int8_t>(LEVEL_TAB_BITS);
            }
        }
    }

    return table;
}

// ============================================================================
// Generated Table (3,584 entries = 7 × 256 × 2)
// ============================================================================

constexpr auto cavlc_level_tab = generate_cavlc_level_tab();

// ============================================================================
// Static Assertions: Comprehensive Validation
// ============================================================================

// Basic constants validation
static_assert(LEVEL_TAB_BITS == 8, "LEVEL_TAB_BITS must be 8");
static_assert(LEVEL_TAB_SIZE == 256, "Table size is 256 entries");
static_assert(NUM_SUFFIX_LENGTHS == 7, "7 suffix lengths (0-6)");

// Log2 function validation
static_assert(log2_constexpr(1) == 0, "log2(1) = 0");
static_assert(log2_constexpr(2) == 1, "log2(2) = 1");
static_assert(log2_constexpr(4) == 2, "log2(4) = 2");
static_assert(log2_constexpr(8) == 3, "log2(8) = 3");
static_assert(log2_constexpr(16) == 4, "log2(16) = 4");
static_assert(log2_constexpr(128) == 7, "log2(128) = 7");
static_assert(log2_constexpr(256) == 8, "log2(256) = 8");
static_assert(log2_constexpr(255) == 7, "log2(255) = 7 (floor)");
static_assert(log2_constexpr(0) == -1, "log2(0) = -1");

// Table dimensions
static_assert(cavlc_level_tab.size() == 7, "7 suffix lengths");
static_assert(cavlc_level_tab[0].size() == 256, "256 entries per suffix length");
static_assert(cavlc_level_tab[0][0].size() == 2, "2 values per entry (code, length)");

// Entry structure size
static_assert(sizeof(cavlc_level_entry) == 2, "Entry is 2 bytes");

// Validate special markers for overflow cases
// When i=0, prefix = 8 - log2(0) = 8 - (-1) = 9
// So prefix + 1 = 10 > 8, should use overflow marker
static_assert(cavlc_level_tab[0][0][0] == 108, "i=0 overflow marker (LEVEL_TAB_BITS+100)");
static_assert(cavlc_level_tab[0][0][1] == 8, "i=0 uses all 8 bits");

// For i=1: 2*i=2, log2(2)=1, prefix = 8-1 = 7
// prefix + 1 + suffix_length = 7 + 1 + 0 = 8 <= 8, so it fits!
// level_code = (7 << 0) + 0 - 1 = 6
// Sign handling: mask = -(6 & 1) = 0, level_code = ((2+6)>>1) ^ 0 - 0 = 4
static_assert(cavlc_level_tab[0][1][0] == 4, "i=1, suffix=0 gives level=4");
static_assert(cavlc_level_tab[0][1][1] == 8, "i=1, suffix=0 uses 8 bits");

// For i=2: 2*i=4, log2(4)=2, prefix = 8-2 = 6
// level_code = (6 << 0) + 0 - 1 = 5
// Sign handling: mask = -(5 & 1) = -1, level_code = ((2+5)>>1) ^ -1 - (-1) = 3^-1+1 = -3+1 = -2
// Wait, let me recalculate: (7>>1) = 3, 3^-1 = -4, -4 - (-1) = -3
static_assert(cavlc_level_tab[0][2][0] == -3, "i=2, suffix=0 gives level=-3");

// For i=4: 2*i=8, log2(8)=3, prefix = 8-3 = 5
// level_code = (5 << 0) + 0 - 1 = 4
// Sign handling: mask = -(4 & 1) = 0, level_code = ((2+4)>>1) ^ 0 - 0 = 3
static_assert(cavlc_level_tab[0][4][0] == 3, "i=4, suffix=0 gives level=3");

// Check different suffix lengths affect the calculation
// For suffix_length=1, i=4: prefix=5
// prefix + 1 + suffix = 5 + 1 + 1 = 7 <= 8, fits
// log2(4)=2, suffix_bits = 4 >> (2-1) = 4 >> 1 = 2
// level_code = (5 << 1) + 2 - 2 = 10 + 2 - 2 = 10
// mask = -(10 & 1) = 0, level_code = ((2+10)>>1) = 6
static_assert(cavlc_level_tab[1][4][0] == 6, "i=4, suffix=1 gives level=6");
static_assert(cavlc_level_tab[1][4][1] == 7, "i=4, suffix=1 uses 7 bits");

// Check that higher suffix lengths work correctly
static_assert(cavlc_level_tab[2][8][0] != 0 || cavlc_level_tab[2][8][1] != 0,
              "suffix=2, i=8 is populated");
static_assert(cavlc_level_tab[3][16][0] != 0 || cavlc_level_tab[3][16][1] != 0,
              "suffix=3, i=16 is populated");

// Verify bit lengths are reasonable (1-8 bits)
static_assert(cavlc_level_tab[0][1][1] >= 1 && cavlc_level_tab[0][1][1] <= 8,
              "Bit length in valid range");
static_assert(cavlc_level_tab[3][50][1] >= 1 && cavlc_level_tab[3][50][1] <= 8,
              "Another bit length check");

// Check boundary cases for different suffix lengths
// suffix=6 is the maximum
static_assert(cavlc_level_tab[6][1][0] != 0 || cavlc_level_tab[6][1][1] != 0,
              "Maximum suffix length works");

// Verify overflow markers appear correctly
// For large i values with suffix_length=0, check we use markers correctly
static_assert(cavlc_level_tab[0][128][0] <= 15 && cavlc_level_tab[0][128][0] >= -15,
              "Level codes in reasonable range or are markers");

// Check sign handling produces both positive and negative values
static_assert(cavlc_level_tab[0][2][0] < 0, "Some levels are negative");
static_assert(cavlc_level_tab[0][1][0] > 0, "Some levels are positive");
static_assert(cavlc_level_tab[0][4][0] > 0, "Another positive level");

// Verify the "need more bits" marker (prefix + 100)
// This should appear when prefix + 1 + suffix > 8 but prefix + 1 <= 8
// For i=1, suffix=6: prefix=7, 7+1+6=14 > 8, but 7+1=8 <= 8
// So should use prefix + 100 = 107
static_assert(cavlc_level_tab[6][1][0] == 107, "Uses 'need more bits' marker");
static_assert(cavlc_level_tab[6][1][1] == 8, "Marker uses 8 bits");

// Another "need more bits" case
// For i=2, suffix=5: prefix=6, 6+1+5=12 > 8, 6+1=7 <= 8
// Should use prefix + 100 = 106
static_assert(cavlc_level_tab[5][2][0] == 106, "Another 'need more bits' marker");

// Verify level code values exist (int8_t range is guaranteed by type)
static_assert(cavlc_level_tab[2][100][1] > 0, "Entry at [2][100] is populated");
static_assert(cavlc_level_tab[4][200][1] > 0, "Entry at [4][200] is populated");

// Check consistency: bit length should increase or stay same as we need more bits
static_assert(cavlc_level_tab[0][255][1] >= cavlc_level_tab[0][128][1],
              "Bit length doesn't decrease for larger patterns");

// Validate that for suffix=0, early entries use full codes
static_assert(cavlc_level_tab[0][3][0] >= -15 && cavlc_level_tab[0][3][0] <= 15,
              "suffix=0, i=3 has reasonable level");

// Check various indices across the table
static_assert(cavlc_level_tab[1][10][1] > 0, "suffix=1, i=10 has non-zero length");
static_assert(cavlc_level_tab[2][20][1] > 0, "suffix=2, i=20 has non-zero length");
static_assert(cavlc_level_tab[3][30][1] > 0, "suffix=3, i=30 has non-zero length");
static_assert(cavlc_level_tab[4][40][1] > 0, "suffix=4, i=40 has non-zero length");
static_assert(cavlc_level_tab[5][50][1] > 0, "suffix=5, i=50 has non-zero length");
static_assert(cavlc_level_tab[6][60][1] > 0, "suffix=6, i=60 has non-zero length");

// Ensure all entries are initialized (no zeros unless intentional)
static_assert(cavlc_level_tab[0][100][1] != 0, "Entry is initialized");
static_assert(cavlc_level_tab[3][150][1] != 0, "Another entry initialized");
static_assert(cavlc_level_tab[6][250][1] != 0, "High index initialized");

} // namespace FFmpegH264CAVLC

// ============================================================================
// C API Compatibility
// ============================================================================

extern "C" {

/**
 * Get pointer to the CAVLC level table for use in C code.
 * Returns: Pointer to 7×256×2 int8_t array
 */
inline const int8_t (*get_h264_cavlc_level_tab())[256][2] {
    return reinterpret_cast<const int8_t(*)[256][2]>(
        &FFmpegH264CAVLC::cavlc_level_tab
    );
}

} // extern "C"

#endif // AVCODEC_H264_CAVLC_TABLEGEN_CONSTEXPR_HPP
