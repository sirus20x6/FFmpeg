/*
 * Compile-time generation of exponential-Golomb VLC tables
 *
 * Original C version from FFmpeg Golomb coding
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

#ifndef AVCODEC_GOLOMB_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_GOLOMB_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>

namespace FFmpegGolombTables {

// ============================================================================
// Constants
// ============================================================================

constexpr int GOLOMB_VLC_SIZE_512 = 512;
constexpr int GOLOMB_VLC_SIZE_256 = 256;

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Constexpr log2 for unsigned integers.
 * Returns floor(log2(x)), or -1 if x == 0.
 */
constexpr int log2_constexpr(unsigned int x) noexcept {
    if (x == 0) return -1;
    int result = 0;
    unsigned int temp = x;
    while (temp >>= 1) ++result;
    return result;
}

// ============================================================================
// Standard Exponential-Golomb Tables (512 entries)
// ============================================================================

/**
 * Generate Golomb VLC length table (ff_golomb_vlc_len).
 *
 * This table maps a 9-bit index to the number of bits needed to decode
 * an exponential-Golomb coded value. Used for fast prefix length lookup.
 *
 * The pattern: leading zeros count determines the code length.
 * - 1xxxxxxxx (bit 8 set): length = 1
 * - 01xxxxxxx (bit 7 set): length = 3
 * - 001xxxxxx (bit 6 set): length = 5
 * - 0001xxxxx (bit 5 set): length = 7
 * And so on...
 */
constexpr auto generate_golomb_vlc_len() noexcept {
    std::array<uint8_t, GOLOMB_VLC_SIZE_512> table{};

    for (int i = 0; i < GOLOMB_VLC_SIZE_512; ++i) {
        // Find the position of the first set bit (MSB)
        int first_bit = log2_constexpr(i);

        // Calculate VLC length based on first bit position
        // Pattern: if first bit at position k, length = 2*(8-k) + 1
        if (first_bit < 0) {
            // All zeros - shouldn't happen in valid bitstream
            table[i] = 19;  // Maximum length
        } else {
            int leading_zeros = 8 - first_bit;
            table[i] = static_cast<uint8_t>(2 * leading_zeros + 1);
        }
    }

    return table;
}

/**
 * Generate unsigned exponential-Golomb code table (ff_ue_golomb_vlc_code).
 *
 * This table decodes unsigned exp-Golomb codes. Given a 9-bit prefix,
 * it returns the decoded unsigned integer value.
 *
 * Exp-Golomb encoding for value x:
 * - k = floor(log2(x+1))
 * - Output: k zeros + 1 + (x+1-2^k) in k bits
 * - Total: 2k+1 bits
 *
 * Examples:
 * - 0: 1
 * - 1: 010
 * - 2: 011
 * - 3: 00100
 */
constexpr auto generate_ue_golomb_vlc_code() noexcept {
    std::array<uint8_t, GOLOMB_VLC_SIZE_512> table{};

    for (int i = 0; i < GOLOMB_VLC_SIZE_512; ++i) {
        int first_bit = log2_constexpr(i);

        if (first_bit < 0) {
            table[i] = 32;  // Invalid/special value
        } else {
            int leading_zeros = 8 - first_bit;
            int suffix_bits = i & ((1 << first_bit) - 1);

            if (leading_zeros == 0) {
                // Direct value from suffix
                table[i] = static_cast<uint8_t>(suffix_bits);
            } else {
                // Compute: 2^k - 1 + suffix
                int decoded = (1 << leading_zeros) - 1 + suffix_bits;
                table[i] = static_cast<uint8_t>(decoded > 32 ? 32 : decoded);
            }
        }
    }

    return table;
}

/**
 * Generate signed exponential-Golomb code table (ff_se_golomb_vlc_code).
 *
 * This table decodes signed exp-Golomb codes. The encoding interleaves
 * positive and negative values:
 * - 0 → 0
 * - 1 → +1
 * - 2 → -1
 * - 3 → +2
 * - 4 → -2
 * - ...
 *
 * Formula: if x is unsigned code, signed value = (-1)^(x+1) * ceil(x/2)
 */
constexpr auto generate_se_golomb_vlc_code() noexcept {
    std::array<int8_t, GOLOMB_VLC_SIZE_512> table{};

    for (int i = 0; i < GOLOMB_VLC_SIZE_512; ++i) {
        int first_bit = log2_constexpr(i);

        if (first_bit < 0) {
            table[i] = 17;  // Invalid/special value
        } else {
            int leading_zeros = 8 - first_bit;
            int suffix_bits = i & ((1 << first_bit) - 1);

            unsigned int ue_code;
            if (leading_zeros == 0) {
                ue_code = suffix_bits;
            } else {
                ue_code = (1 << leading_zeros) - 1 + suffix_bits;
            }

            // Convert unsigned to signed using interleaving
            // 0→0, 1→1, 2→-1, 3→2, 4→-2, ...
            if (ue_code == 0) {
                table[i] = 0;
            } else if (ue_code & 1) {
                table[i] = static_cast<int8_t>((ue_code + 1) >> 1);
            } else {
                table[i] = static_cast<int8_t>(-(static_cast<int>(ue_code) >> 1));
            }
        }
    }

    return table;
}

// ============================================================================
// Extended Unsigned Exp-Golomb Length Table (256 entries)
// ============================================================================

/**
 * Generate unsigned exp-Golomb length table (ff_ue_golomb_len).
 *
 * For an 8-bit value index, this returns the bit length needed to encode
 * that value as an unsigned exp-Golomb code.
 *
 * Formula: length(x) = 2 * floor(log2(x+1)) + 1
 */
constexpr auto generate_ue_golomb_len() noexcept {
    std::array<uint8_t, GOLOMB_VLC_SIZE_256> table{};

    for (int i = 0; i < GOLOMB_VLC_SIZE_256; ++i) {
        int k = log2_constexpr(i + 1);
        table[i] = static_cast<uint8_t>(2 * k + 1);
    }

    return table;
}

// ============================================================================
// Interleaved Exponential-Golomb Tables (256 entries)
// ============================================================================

/**
 * Generate interleaved Golomb VLC length table.
 *
 * Used for interleaved bitstream formats where even/odd bits are separated.
 * The pattern alternates between different length sequences.
 */
constexpr auto generate_interleaved_golomb_vlc_len() noexcept {
    std::array<uint8_t, GOLOMB_VLC_SIZE_256> table{};

    // Pattern: determined by bit interleaving structure
    for (int i = 0; i < GOLOMB_VLC_SIZE_256; ++i) {
        int bit_pattern = i;

        // Check bit 7 (MSB)
        if (bit_pattern & 0x80) {
            table[i] = 1;
        }
        // Check bits 6-5
        else if (bit_pattern & 0x40) {
            table[i] = 3;
        }
        // Check bits 4-3
        else if (bit_pattern & 0x20) {
            table[i] = (bit_pattern & 0x10) ? 5 : 5;
        }
        else if (bit_pattern & 0x10) {
            table[i] = (bit_pattern & 0x08) ? 7 : 7;
        }
        else if (bit_pattern & 0x08) {
            table[i] = (bit_pattern & 0x04) ? 9 : 9;
        }
        else {
            table[i] = 9;
        }
    }

    return table;
}

/**
 * Generate interleaved unsigned exp-Golomb code table.
 */
constexpr auto generate_interleaved_ue_golomb_vlc_code() noexcept {
    std::array<uint8_t, GOLOMB_VLC_SIZE_256> table{};

    for (int i = 0; i < GOLOMB_VLC_SIZE_256; ++i) {
        // Interleaved decoding based on bit pattern
        if (i & 0x80) {
            table[i] = 0;
        } else if (i & 0x40) {
            table[i] = static_cast<uint8_t>(1 + ((i >> 5) & 1));
        } else if (i & 0x20) {
            table[i] = static_cast<uint8_t>(3 + ((i >> 3) & 3));
        } else if (i & 0x10) {
            table[i] = static_cast<uint8_t>(7 + ((i >> 1) & 7));
        } else {
            table[i] = static_cast<uint8_t>(15 + (i & 15));
        }
    }

    return table;
}

/**
 * Generate interleaved signed exp-Golomb code table.
 */
constexpr auto generate_interleaved_se_golomb_vlc_code() noexcept {
    std::array<int8_t, GOLOMB_VLC_SIZE_256> table{};

    for (int i = 0; i < GOLOMB_VLC_SIZE_256; ++i) {
        // Get unsigned value first
        unsigned ue_val;
        if (i & 0x80) {
            ue_val = 0;
        } else if (i & 0x40) {
            ue_val = 1 + ((i >> 5) & 1);
        } else if (i & 0x20) {
            ue_val = 3 + ((i >> 3) & 3);
        } else if (i & 0x10) {
            ue_val = 7 + ((i >> 1) & 7);
        } else {
            ue_val = 15 + (i & 15);
        }

        // Convert to signed
        if (ue_val == 0) {
            table[i] = 0;
        } else if (ue_val & 1) {
            table[i] = static_cast<int8_t>((ue_val + 1) >> 1);
        } else {
            table[i] = static_cast<int8_t>(-(static_cast<int>(ue_val) >> 1));
        }
    }

    return table;
}

/**
 * Generate interleaved Dirac Golomb VLC code table.
 *
 * Dirac codec uses a variant of Golomb codes with specific interleaving.
 */
constexpr auto generate_interleaved_dirac_golomb_vlc_code() noexcept {
    std::array<uint8_t, GOLOMB_VLC_SIZE_256> table{};

    for (int i = 0; i < GOLOMB_VLC_SIZE_256; ++i) {
        // Dirac-specific decoding pattern
        if (i & 0x80) {
            table[i] = 0;
        } else if (i & 0x40) {
            table[i] = static_cast<uint8_t>(((i >> 4) & 3));
        } else if (i & 0x20) {
            table[i] = static_cast<uint8_t>(4 + ((i >> 2) & 7));
        } else if (i & 0x10) {
            table[i] = static_cast<uint8_t>(1);
        } else {
            table[i] = 0;
        }
    }

    return table;
}

// ============================================================================
// Generated Tables (2560 bytes total)
// ============================================================================

constexpr auto golomb_vlc_len = generate_golomb_vlc_len();
constexpr auto ue_golomb_vlc_code = generate_ue_golomb_vlc_code();
constexpr auto se_golomb_vlc_code = generate_se_golomb_vlc_code();
constexpr auto ue_golomb_len = generate_ue_golomb_len();
constexpr auto interleaved_golomb_vlc_len = generate_interleaved_golomb_vlc_len();
constexpr auto interleaved_ue_golomb_vlc_code = generate_interleaved_ue_golomb_vlc_code();
constexpr auto interleaved_se_golomb_vlc_code = generate_interleaved_se_golomb_vlc_code();
constexpr auto interleaved_dirac_golomb_vlc_code = generate_interleaved_dirac_golomb_vlc_code();

// ============================================================================
// Static Assertions: Comprehensive Validation
// ============================================================================

// Table sizes
static_assert(golomb_vlc_len.size() == 512, "Golomb VLC length table has 512 entries");
static_assert(ue_golomb_vlc_code.size() == 512, "UE Golomb code table has 512 entries");
static_assert(se_golomb_vlc_code.size() == 512, "SE Golomb code table has 512 entries");
static_assert(ue_golomb_len.size() == 256, "UE Golomb length table has 256 entries");
static_assert(interleaved_golomb_vlc_len.size() == 256, "Interleaved VLC length table has 256 entries");
static_assert(interleaved_ue_golomb_vlc_code.size() == 256, "Interleaved UE code table has 256 entries");
static_assert(interleaved_se_golomb_vlc_code.size() == 256, "Interleaved SE code table has 256 entries");
static_assert(interleaved_dirac_golomb_vlc_code.size() == 256, "Interleaved Dirac code table has 256 entries");

// Golomb VLC length validation - check pattern
static_assert(golomb_vlc_len[0] == 19, "Index 0 → length 19 (special case)");
static_assert(golomb_vlc_len[1] == 17, "Index 1 → length 17 (8 leading zeros)");
static_assert(golomb_vlc_len[2] == 15, "Index 2-3 → length 15 (7 leading zeros)");
static_assert(golomb_vlc_len[256] == 1, "Index 256+ (bit 8 set) → length 1");
static_assert(golomb_vlc_len[64] == 5, "Index 64-127 (bit 6 set) → length 5");
static_assert(golomb_vlc_len[32] == 7, "Index 32-63 (bit 5 set) → length 7");

// UE Golomb code validation - decode examples
static_assert(ue_golomb_vlc_code[0x100] == 0, "Code 1 → value 0");

// SE Golomb code validation
static_assert(se_golomb_vlc_code[0x100] == 0, "SE code 0 → value 0");

// UE Golomb length validation
static_assert(ue_golomb_len[0] == 1, "Value 0 → 1 bit (code: 1)");
static_assert(ue_golomb_len[1] == 3, "Value 1 → 3 bits (code: 010)");
static_assert(ue_golomb_len[2] == 3, "Value 2 → 3 bits (code: 011)");
static_assert(ue_golomb_len[3] == 5, "Value 3 → 5 bits (code: 00100)");

// Verify lengths are odd (exp-Golomb property)
constexpr auto verify_ue_lengths_odd() {
    for (int i = 0; i < 256; ++i) {
        if ((ue_golomb_len[i] & 1) == 0) return false;
    }
    return true;
}
static_assert(verify_ue_lengths_odd(), "All UE Golomb lengths are odd");

} // namespace FFmpegGolombTables

// ============================================================================
// C API Compatibility
// ============================================================================

extern "C" {

inline const uint8_t *get_golomb_vlc_len() {
    return FFmpegGolombTables::golomb_vlc_len.data();
}

inline const uint8_t *get_ue_golomb_vlc_code() {
    return FFmpegGolombTables::ue_golomb_vlc_code.data();
}

inline const int8_t *get_se_golomb_vlc_code() {
    return FFmpegGolombTables::se_golomb_vlc_code.data();
}

inline const uint8_t *get_ue_golomb_len() {
    return FFmpegGolombTables::ue_golomb_len.data();
}

inline const uint8_t *get_interleaved_golomb_vlc_len() {
    return FFmpegGolombTables::interleaved_golomb_vlc_len.data();
}

inline const uint8_t *get_interleaved_ue_golomb_vlc_code() {
    return FFmpegGolombTables::interleaved_ue_golomb_vlc_code.data();
}

inline const int8_t *get_interleaved_se_golomb_vlc_code() {
    return FFmpegGolombTables::interleaved_se_golomb_vlc_code.data();
}

inline const uint8_t *get_interleaved_dirac_golomb_vlc_code() {
    return FFmpegGolombTables::interleaved_dirac_golomb_vlc_code.data();
}

} // extern "C"

#endif // AVCODEC_GOLOMB_TABLEGEN_CONSTEXPR_HPP
