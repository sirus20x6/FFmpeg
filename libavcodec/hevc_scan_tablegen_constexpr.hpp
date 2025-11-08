/*
 * Compile-time generation of HEVC diagonal scan patterns
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

#ifndef AVCODEC_HEVC_SCAN_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_HEVC_SCAN_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>

namespace FFmpegHEVCScan {

// ============================================================================
// Constants
// ============================================================================

constexpr int HEVC_SCAN_4x4_SIZE = 16;   // 4×4 block = 16 coefficients
constexpr int HEVC_SCAN_8x8_SIZE = 64;   // 8×8 block = 64 coefficients

// ============================================================================
// HEVC Diagonal Scan Pattern Generation
// ============================================================================

/**
 * Generate HEVC diagonal scan pattern for N×N transform blocks.
 *
 * HEVC (H.265) uses diagonal scan patterns to process DCT coefficients in
 * zigzag order from low-frequency (top-left) to high-frequency (bottom-right).
 * This ordering improves entropy coding efficiency by grouping similar
 * coefficient magnitudes together.
 *
 * Diagonal scan pattern for 4×4 block:
 *   Position indices:       Scan order:
 *   [ 0  1  2  3]          [ 0  1  3  6]
 *   [ 4  5  6  7]          [ 2  4  7 10]
 *   [ 8  9 10 11]          [ 5  8 11 13]
 *   [12 13 14 15]          [ 9 12 14 15]
 *
 * The scan proceeds along diagonals (45° angle), alternating direction:
 * - Diagonal 0: (0,0)
 * - Diagonal 1: (0,1), (1,0)
 * - Diagonal 2: (2,0), (1,1), (0,2)
 * - Diagonal 3: (0,3), (1,2), (2,1), (3,0)
 * - etc.
 *
 * For each diagonal, the scan goes from bottom-left to top-right.
 *
 * Template parameters:
 * - N: Block size (4 or 8)
 * - CoordType: Either X or Y coordinates
 */
template<int N>
constexpr auto generate_diag_scan(bool get_x) noexcept {
    std::array<uint8_t, N * N> scan{};
    int idx = 0;

    // Process each diagonal (sum of x+y coordinates)
    for (int diag = 0; diag < 2 * N - 1; ++diag) {
        // For each diagonal, scan from bottom-left to top-right
        // Start y at min(diag, N-1) and decrease
        int y_start = (diag < N) ? diag : N - 1;
        int x_start = (diag < N) ? 0 : diag - (N - 1);

        for (int i = 0; i <= y_start - x_start; ++i) {
            int x = x_start + i;
            int y = y_start - i;

            if (x < N && y >= 0 && y < N) {
                scan[idx++] = get_x ? static_cast<uint8_t>(x) : static_cast<uint8_t>(y);
            }
        }
    }

    return scan;
}

// ============================================================================
// HEVC 4×4 Diagonal Scan Patterns (32 bytes)
// ============================================================================

/**
 * HEVC 4×4 diagonal scan X coordinates.
 *
 * For each position in scan order (0-15), provides the X coordinate
 * of the coefficient in the 4×4 block. Used with Y coordinates to
 * locate coefficients during entropy coding/decoding.
 *
 * Example: scan_x[0] = 0, scan_y[0] = 0 → first coefficient is at (0,0)
 *          scan_x[1] = 0, scan_y[1] = 1 → second coefficient is at (0,1)
 */
constexpr auto hevc_diag_scan4x4_x = generate_diag_scan<4>(true);

/**
 * HEVC 4×4 diagonal scan Y coordinates.
 *
 * Companion to scan_x providing Y coordinates for each scan position.
 */
constexpr auto hevc_diag_scan4x4_y = generate_diag_scan<4>(false);

// ============================================================================
// HEVC 8×8 Diagonal Scan Patterns (128 bytes)
// ============================================================================

/**
 * HEVC 8×8 diagonal scan X coordinates.
 *
 * Extends diagonal scan pattern to 8×8 transform blocks (High Efficiency mode).
 * Larger blocks require more complex scan patterns but follow the same
 * diagonal principle.
 */
constexpr auto hevc_diag_scan8x8_x = generate_diag_scan<8>(true);

/**
 * HEVC 8×8 diagonal scan Y coordinates.
 *
 * Companion Y coordinates for 8×8 block diagonal scan.
 */
constexpr auto hevc_diag_scan8x8_y = generate_diag_scan<8>(false);

// ============================================================================
// Static Assertions: Comprehensive Validation
// ============================================================================

// Table sizes
static_assert(hevc_diag_scan4x4_x.size() == 16, "4×4 scan has 16 entries");
static_assert(hevc_diag_scan4x4_y.size() == 16, "4×4 scan has 16 entries");
static_assert(hevc_diag_scan8x8_x.size() == 64, "8×8 scan has 64 entries");
static_assert(hevc_diag_scan8x8_y.size() == 64, "8×8 scan has 64 entries");

// 4×4 scan pattern verification
// First coefficient: DC at (0,0)
static_assert(hevc_diag_scan4x4_x[0] == 0 && hevc_diag_scan4x4_y[0] == 0,
              "First coefficient is at (0,0) - DC");

// Second coefficient: (0,1) - first AC
static_assert(hevc_diag_scan4x4_x[1] == 0 && hevc_diag_scan4x4_y[1] == 1,
              "Second coefficient is at (0,1)");

// Third coefficient: (1,0)
static_assert(hevc_diag_scan4x4_x[2] == 1 && hevc_diag_scan4x4_y[2] == 0,
              "Third coefficient is at (1,0)");

// Last coefficient: (3,3) - highest frequency
static_assert(hevc_diag_scan4x4_x[15] == 3 && hevc_diag_scan4x4_y[15] == 3,
              "Last coefficient is at (3,3) - highest frequency");

// Verify some diagonal pattern positions
static_assert(hevc_diag_scan4x4_x[3] == 0 && hevc_diag_scan4x4_y[3] == 2,
              "4th position: (0,2)");
static_assert(hevc_diag_scan4x4_x[4] == 1 && hevc_diag_scan4x4_y[4] == 1,
              "5th position: (1,1)");
static_assert(hevc_diag_scan4x4_x[5] == 2 && hevc_diag_scan4x4_y[5] == 0,
              "6th position: (2,0)");

// 8×8 scan pattern verification
// First coefficient: DC at (0,0)
static_assert(hevc_diag_scan8x8_x[0] == 0 && hevc_diag_scan8x8_y[0] == 0,
              "8×8: First coefficient is at (0,0) - DC");

// Last coefficient: (7,7) - highest frequency
static_assert(hevc_diag_scan8x8_x[63] == 7 && hevc_diag_scan8x8_y[63] == 7,
              "8×8: Last coefficient is at (7,7) - highest frequency");

// Verify 8×8 diagonal progression
static_assert(hevc_diag_scan8x8_x[1] == 0 && hevc_diag_scan8x8_y[1] == 1,
              "8×8: 2nd position (0,1)");
static_assert(hevc_diag_scan8x8_x[2] == 1 && hevc_diag_scan8x8_y[2] == 0,
              "8×8: 3rd position (1,0)");

// Verify mid-range 8×8 positions
static_assert(hevc_diag_scan8x8_x[10] == 0 && hevc_diag_scan8x8_y[10] == 4,
              "8×8: 11th position (0,4)");
static_assert(hevc_diag_scan8x8_x[20] == 5 && hevc_diag_scan8x8_y[20] == 0,
              "8×8: 21st position (5,0)");

// Verify all coordinates are in valid range
constexpr bool verify_4x4_coordinates() {
    for (int i = 0; i < 16; ++i) {
        if (hevc_diag_scan4x4_x[i] >= 4 || hevc_diag_scan4x4_y[i] >= 4) {
            return false;
        }
    }
    return true;
}
static_assert(verify_4x4_coordinates(), "All 4×4 coordinates in range [0,3]");

constexpr bool verify_8x8_coordinates() {
    for (int i = 0; i < 64; ++i) {
        if (hevc_diag_scan8x8_x[i] >= 8 || hevc_diag_scan8x8_y[i] >= 8) {
            return false;
        }
    }
    return true;
}
static_assert(verify_8x8_coordinates(), "All 8×8 coordinates in range [0,7]");

// Verify no duplicate positions in 4×4 scan
constexpr bool verify_4x4_no_duplicates() {
    for (int i = 0; i < 16; ++i) {
        for (int j = i + 1; j < 16; ++j) {
            if (hevc_diag_scan4x4_x[i] == hevc_diag_scan4x4_x[j] &&
                hevc_diag_scan4x4_y[i] == hevc_diag_scan4x4_y[j]) {
                return false;  // Found duplicate
            }
        }
    }
    return true;
}
static_assert(verify_4x4_no_duplicates(), "4×4 scan has no duplicate positions");

// Verify no duplicate positions in 8×8 scan
constexpr bool verify_8x8_no_duplicates() {
    for (int i = 0; i < 64; ++i) {
        for (int j = i + 1; j < 64; ++j) {
            if (hevc_diag_scan8x8_x[i] == hevc_diag_scan8x8_x[j] &&
                hevc_diag_scan8x8_y[i] == hevc_diag_scan8x8_y[j]) {
                return false;  // Found duplicate
            }
        }
    }
    return true;
}
static_assert(verify_8x8_no_duplicates(), "8×8 scan has no duplicate positions");

// Verify all positions covered in 4×4 scan
constexpr bool verify_4x4_complete() {
    // Check that every (x,y) position appears exactly once
    bool covered[4][4] = {};
    for (int i = 0; i < 16; ++i) {
        covered[hevc_diag_scan4x4_x[i]][hevc_diag_scan4x4_y[i]] = true;
    }
    for (int x = 0; x < 4; ++x) {
        for (int y = 0; y < 4; ++y) {
            if (!covered[x][y]) return false;
        }
    }
    return true;
}
static_assert(verify_4x4_complete(), "4×4 scan covers all positions");

// Verify all positions covered in 8×8 scan
constexpr bool verify_8x8_complete() {
    bool covered[8][8] = {};
    for (int i = 0; i < 64; ++i) {
        covered[hevc_diag_scan8x8_x[i]][hevc_diag_scan8x8_y[i]] = true;
    }
    for (int x = 0; x < 8; ++x) {
        for (int y = 0; y < 8; ++y) {
            if (!covered[x][y]) return false;
        }
    }
    return true;
}
static_assert(verify_8x8_complete(), "8×8 scan covers all positions");

// Verify diagonal property: sum of coordinates increases along scan
constexpr bool verify_4x4_diagonal_order() {
    for (int i = 0; i < 15; ++i) {
        int sum_i = hevc_diag_scan4x4_x[i] + hevc_diag_scan4x4_y[i];
        int sum_next = hevc_diag_scan4x4_x[i+1] + hevc_diag_scan4x4_y[i+1];
        // Sum should be non-decreasing (same diagonal or next diagonal)
        if (sum_i > sum_next) return false;
    }
    return true;
}
static_assert(verify_4x4_diagonal_order(), "4×4 scan follows diagonal order");

constexpr bool verify_8x8_diagonal_order() {
    for (int i = 0; i < 63; ++i) {
        int sum_i = hevc_diag_scan8x8_x[i] + hevc_diag_scan8x8_y[i];
        int sum_next = hevc_diag_scan8x8_x[i+1] + hevc_diag_scan8x8_y[i+1];
        if (sum_i > sum_next) return false;
    }
    return true;
}
static_assert(verify_8x8_diagonal_order(), "8×8 scan follows diagonal order");

} // namespace FFmpegHEVCScan

// ============================================================================
// C API Compatibility
// ============================================================================

extern "C" {

/**
 * Get pointer to HEVC 4×4 diagonal scan X coordinates.
 * Returns: Pointer to 16-element uint8_t array
 */
inline const uint8_t *get_hevc_diag_scan4x4_x() {
    return FFmpegHEVCScan::hevc_diag_scan4x4_x.data();
}

/**
 * Get pointer to HEVC 4×4 diagonal scan Y coordinates.
 * Returns: Pointer to 16-element uint8_t array
 */
inline const uint8_t *get_hevc_diag_scan4x4_y() {
    return FFmpegHEVCScan::hevc_diag_scan4x4_y.data();
}

/**
 * Get pointer to HEVC 8×8 diagonal scan X coordinates.
 * Returns: Pointer to 64-element uint8_t array
 */
inline const uint8_t *get_hevc_diag_scan8x8_x() {
    return FFmpegHEVCScan::hevc_diag_scan8x8_x.data();
}

/**
 * Get pointer to HEVC 8×8 diagonal scan Y coordinates.
 * Returns: Pointer to 64-element uint8_t array
 */
inline const uint8_t *get_hevc_diag_scan8x8_y() {
    return FFmpegHEVCScan::hevc_diag_scan8x8_y.data();
}

} // extern "C"

#endif // AVCODEC_HEVC_SCAN_TABLEGEN_CONSTEXPR_HPP
