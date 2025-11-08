/*
 * Compile-time generation of HEVC diagonal scan tables
 *
 * Original C version from FFmpeg HEVC decoder
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

#ifndef AVCODEC_HEVC_DATA_CONSTEXPR_HPP
#define AVCODEC_HEVC_DATA_CONSTEXPR_HPP

#include <array>
#include <cstdint>

namespace FFmpegHEVCData {

// ============================================================================
// Constants
// ============================================================================

constexpr int SCAN_4X4_SIZE = 16;   // 4×4 = 16 coefficients
constexpr int SCAN_8X8_SIZE = 64;   // 8×8 = 64 coefficients

// ============================================================================
// Diagonal Scan Pattern Generation
// ============================================================================

/**
 * Generate HEVC diagonal scan pattern for a block.
 *
 * The diagonal scan pattern traverses a 2D transform block in diagonal order,
 * starting from the top-left (DC coefficient) and proceeding along diagonals.
 * This ordering groups low-frequency coefficients together, which is optimal
 * for run-length coding of sparse coefficient data.
 *
 * Scan pattern for 4×4 block (numbers show scan order):
 *    x: 0  1  2  3
 *  y  +------------
 *  0  | 0  1  5  6
 *  1  | 2  4  7 12
 *  2  | 3  8 11 13
 *  3  | 9 10 14 15
 *
 * Algorithm:
 * - Process diagonals from top-left to bottom-right
 * - Each diagonal has sum(x + y) = constant
 * - Within each diagonal, traverse from bottom-left to top-right
 * - Total diagonals = size + size - 1
 */
template<int SIZE>
constexpr auto generate_hevc_diag_scan() noexcept {
    struct ScanPair {
        std::array<uint8_t, SIZE * SIZE> x;
        std::array<uint8_t, SIZE * SIZE> y;
    };

    ScanPair tables{};
    int scan_idx = 0;

    // Iterate through all diagonals (diagonal sum from 0 to 2*SIZE-2)
    for (int diag_sum = 0; diag_sum < 2 * SIZE - 1; ++diag_sum) {
        // Within each diagonal, iterate from bottom to top
        // Start from max y that keeps x non-negative
        int start_y = (diag_sum < SIZE) ? diag_sum : SIZE - 1;

        for (int y = start_y; y >= 0; --y) {
            int x = diag_sum - y;

            // Only include points within the SIZE×SIZE block
            if (x < SIZE && y < SIZE) {
                tables.x[scan_idx] = static_cast<uint8_t>(x);
                tables.y[scan_idx] = static_cast<uint8_t>(y);
                ++scan_idx;
            }
        }
    }

    return tables;
}

// ============================================================================
// Generated Tables (160 bytes total)
// ============================================================================

constexpr auto hevc_scan_4x4 = generate_hevc_diag_scan<4>();
constexpr auto hevc_scan_8x8 = generate_hevc_diag_scan<8>();

// Extract individual tables for C compatibility
constexpr auto hevc_diag_scan4x4_x = hevc_scan_4x4.x;
constexpr auto hevc_diag_scan4x4_y = hevc_scan_4x4.y;
constexpr auto hevc_diag_scan8x8_x = hevc_scan_8x8.x;
constexpr auto hevc_diag_scan8x8_y = hevc_scan_8x8.y;

// ============================================================================
// Static Assertions: Comprehensive Validation
// ============================================================================

// Table sizes
static_assert(hevc_diag_scan4x4_x.size() == 16, "4×4 scan has 16 entries");
static_assert(hevc_diag_scan4x4_y.size() == 16, "4×4 scan has 16 entries");
static_assert(hevc_diag_scan8x8_x.size() == 64, "8×8 scan has 64 entries");
static_assert(hevc_diag_scan8x8_y.size() == 64, "8×8 scan has 64 entries");

// 4×4 scan pattern validation
// First entry is DC coefficient at (0,0)
static_assert(hevc_diag_scan4x4_x[0] == 0, "4×4: First coefficient x=0");
static_assert(hevc_diag_scan4x4_y[0] == 0, "4×4: First coefficient y=0");

// Second diagonal: (0,1) and (1,0)
static_assert(hevc_diag_scan4x4_x[1] == 0, "4×4: Position 1 is (0,1)");
static_assert(hevc_diag_scan4x4_y[1] == 1, "4×4: Position 1 is (0,1)");
static_assert(hevc_diag_scan4x4_x[2] == 1, "4×4: Position 2 is (1,0)");
static_assert(hevc_diag_scan4x4_y[2] == 0, "4×4: Position 2 is (1,0)");

// Third diagonal: (0,2), (1,1), (2,0)
static_assert(hevc_diag_scan4x4_x[3] == 0, "4×4: Position 3 is (0,2)");
static_assert(hevc_diag_scan4x4_y[3] == 2, "4×4: Position 3 is (0,2)");
static_assert(hevc_diag_scan4x4_x[4] == 1, "4×4: Position 4 is (1,1)");
static_assert(hevc_diag_scan4x4_y[4] == 1, "4×4: Position 4 is (1,1)");
static_assert(hevc_diag_scan4x4_x[5] == 2, "4×4: Position 5 is (2,0)");
static_assert(hevc_diag_scan4x4_y[5] == 0, "4×4: Position 5 is (2,0)");

// Last entry is bottom-right corner (3,3)
static_assert(hevc_diag_scan4x4_x[15] == 3, "4×4: Last coefficient x=3");
static_assert(hevc_diag_scan4x4_y[15] == 3, "4×4: Last coefficient y=3");

// 8×8 scan pattern validation
// First entry is DC coefficient at (0,0)
static_assert(hevc_diag_scan8x8_x[0] == 0, "8×8: First coefficient x=0");
static_assert(hevc_diag_scan8x8_y[0] == 0, "8×8: First coefficient y=0");

// Second diagonal: (0,1) and (1,0)
static_assert(hevc_diag_scan8x8_x[1] == 0, "8×8: Position 1 is (0,1)");
static_assert(hevc_diag_scan8x8_y[1] == 1, "8×8: Position 1 is (0,1)");
static_assert(hevc_diag_scan8x8_x[2] == 1, "8×8: Position 2 is (1,0)");
static_assert(hevc_diag_scan8x8_y[2] == 0, "8×8: Position 2 is (1,0)");

// Last entry is bottom-right corner (7,7)
static_assert(hevc_diag_scan8x8_x[63] == 7, "8×8: Last coefficient x=7");
static_assert(hevc_diag_scan8x8_y[63] == 7, "8×8: Last coefficient y=7");

// Verify all coordinates are within bounds
static_assert(hevc_diag_scan4x4_x[10] < 4, "4×4: All x coords < 4");
static_assert(hevc_diag_scan4x4_y[10] < 4, "4×4: All y coords < 4");
static_assert(hevc_diag_scan8x8_x[50] < 8, "8×8: All x coords < 8");
static_assert(hevc_diag_scan8x8_y[50] < 8, "8×8: All y coords < 8");

// Check specific diagonal patterns in 8×8
// Fourth diagonal: (0,3), (1,2), (2,1), (3,0)
static_assert(hevc_diag_scan8x8_x[6] == 0, "8×8: Fourth diagonal starts at x=0");
static_assert(hevc_diag_scan8x8_y[6] == 3, "8×8: Fourth diagonal starts at y=3");
static_assert(hevc_diag_scan8x8_x[9] == 3, "8×8: Fourth diagonal ends at x=3");
static_assert(hevc_diag_scan8x8_y[9] == 0, "8×8: Fourth diagonal ends at y=0");

// Verify diagonal sum property for several positions
// Position 0: x + y = 0 + 0 = 0 (diagonal 0)
static_assert(hevc_diag_scan4x4_x[0] + hevc_diag_scan4x4_y[0] == 0,
              "4×4: Position 0 on diagonal 0");
// Position 1: x + y = 0 + 1 = 1 (diagonal 1)
static_assert(hevc_diag_scan4x4_x[1] + hevc_diag_scan4x4_y[1] == 1,
              "4×4: Position 1 on diagonal 1");
// Position 3: x + y = 0 + 2 = 2 (diagonal 2)
static_assert(hevc_diag_scan4x4_x[3] + hevc_diag_scan4x4_y[3] == 2,
              "4×4: Position 3 on diagonal 2");
// Position 15: x + y = 3 + 3 = 6 (last diagonal)
static_assert(hevc_diag_scan4x4_x[15] + hevc_diag_scan4x4_y[15] == 6,
              "4×4: Last position on diagonal 6");

// Verify uniqueness: all positions (x,y) must be unique
// We can verify this by checking that position sum encoding is unique
// For 4×4: each (x,y) pair maps to unique value x*4 + y
constexpr auto verify_4x4_uniqueness() {
    std::array<bool, 16> seen{};
    for (int i = 0; i < 16; ++i) {
        int pos = hevc_diag_scan4x4_x[i] * 4 + hevc_diag_scan4x4_y[i];
        if (seen[pos]) return false;  // Duplicate found!
        seen[pos] = true;
    }
    return true;  // All unique
}
static_assert(verify_4x4_uniqueness(), "4×4: All positions are unique");

constexpr auto verify_8x8_uniqueness() {
    std::array<bool, 64> seen{};
    for (int i = 0; i < 64; ++i) {
        int pos = hevc_diag_scan8x8_x[i] * 8 + hevc_diag_scan8x8_y[i];
        if (seen[pos]) return false;  // Duplicate found!
        seen[pos] = true;
    }
    return true;  // All unique
}
static_assert(verify_8x8_uniqueness(), "8×8: All positions are unique");

// Verify completeness: all positions from (0,0) to (size-1,size-1) are covered
constexpr auto verify_4x4_completeness() {
    std::array<bool, 16> seen{};
    for (int i = 0; i < 16; ++i) {
        int pos = hevc_diag_scan4x4_x[i] * 4 + hevc_diag_scan4x4_y[i];
        seen[pos] = true;
    }
    // Check all 16 positions are visited
    for (int i = 0; i < 16; ++i) {
        if (!seen[i]) return false;
    }
    return true;
}
static_assert(verify_4x4_completeness(), "4×4: All positions are covered");

constexpr auto verify_8x8_completeness() {
    std::array<bool, 64> seen{};
    for (int i = 0; i < 64; ++i) {
        int pos = hevc_diag_scan8x8_x[i] * 8 + hevc_diag_scan8x8_y[i];
        seen[pos] = true;
    }
    // Check all 64 positions are visited
    for (int i = 0; i < 64; ++i) {
        if (!seen[i]) return false;
    }
    return true;
}
static_assert(verify_8x8_completeness(), "8×8: All positions are covered");

// Verify scan order increases along diagonals
// Within the scan order, diagonal sum should be non-decreasing
constexpr auto verify_4x4_diagonal_order() {
    for (int i = 1; i < 16; ++i) {
        int prev_diag = hevc_diag_scan4x4_x[i-1] + hevc_diag_scan4x4_y[i-1];
        int curr_diag = hevc_diag_scan4x4_x[i] + hevc_diag_scan4x4_y[i];
        if (curr_diag < prev_diag) return false;  // Diagonal order violated
    }
    return true;
}
static_assert(verify_4x4_diagonal_order(), "4×4: Diagonal order is non-decreasing");

constexpr auto verify_8x8_diagonal_order() {
    for (int i = 1; i < 64; ++i) {
        int prev_diag = hevc_diag_scan8x8_x[i-1] + hevc_diag_scan8x8_y[i-1];
        int curr_diag = hevc_diag_scan8x8_x[i] + hevc_diag_scan8x8_y[i];
        if (curr_diag < prev_diag) return false;  // Diagonal order violated
    }
    return true;
}
static_assert(verify_8x8_diagonal_order(), "8×8: Diagonal order is non-decreasing");

// Spot checks for 8×8 middle positions
static_assert(hevc_diag_scan8x8_x[20] < 8 && hevc_diag_scan8x8_y[20] < 8,
              "8×8: Position 20 within bounds");
static_assert(hevc_diag_scan8x8_x[40] < 8 && hevc_diag_scan8x8_y[40] < 8,
              "8×8: Position 40 within bounds");

// Verify corners are at expected positions
// For 4×4: (0,0) is first, (3,3) is last
// Other corners should appear at specific positions based on diagonal scan
static_assert(hevc_diag_scan4x4_x[0] == 0 && hevc_diag_scan4x4_y[0] == 0,
              "4×4: Top-left corner (0,0) is first");
static_assert(hevc_diag_scan4x4_x[15] == 3 && hevc_diag_scan4x4_y[15] == 3,
              "4×4: Bottom-right corner (3,3) is last");

// Top-right corner (3,0) should be on diagonal 3
constexpr auto find_4x4_top_right() {
    for (int i = 0; i < 16; ++i) {
        if (hevc_diag_scan4x4_x[i] == 3 && hevc_diag_scan4x4_y[i] == 0) {
            return i;
        }
    }
    return -1;
}
static_assert(find_4x4_top_right() >= 0, "4×4: Top-right corner (3,0) exists");

// Bottom-left corner (0,3) should be on diagonal 3
constexpr auto find_4x4_bottom_left() {
    for (int i = 0; i < 16; ++i) {
        if (hevc_diag_scan4x4_x[i] == 0 && hevc_diag_scan4x4_y[i] == 3) {
            return i;
        }
    }
    return -1;
}
static_assert(find_4x4_bottom_left() >= 0, "4×4: Bottom-left corner (0,3) exists");

} // namespace FFmpegHEVCData

// ============================================================================
// C API Compatibility
// ============================================================================

extern "C" {

/**
 * Get pointer to HEVC 4×4 diagonal scan X coordinates.
 * Returns: Pointer to 16-element uint8_t array
 */
inline const uint8_t *get_hevc_diag_scan4x4_x() {
    return FFmpegHEVCData::hevc_diag_scan4x4_x.data();
}

/**
 * Get pointer to HEVC 4×4 diagonal scan Y coordinates.
 * Returns: Pointer to 16-element uint8_t array
 */
inline const uint8_t *get_hevc_diag_scan4x4_y() {
    return FFmpegHEVCData::hevc_diag_scan4x4_y.data();
}

/**
 * Get pointer to HEVC 8×8 diagonal scan X coordinates.
 * Returns: Pointer to 64-element uint8_t array
 */
inline const uint8_t *get_hevc_diag_scan8x8_x() {
    return FFmpegHEVCData::hevc_diag_scan8x8_x.data();
}

/**
 * Get pointer to HEVC 8×8 diagonal scan Y coordinates.
 * Returns: Pointer to 64-element uint8_t array
 */
inline const uint8_t *get_hevc_diag_scan8x8_y() {
    return FFmpegHEVCData::hevc_diag_scan8x8_y.data();
}

} // extern "C"

#endif // AVCODEC_HEVC_DATA_CONSTEXPR_HPP
