/*
 * Compile-time generation of ALAC channel layout tables
 *
 * Original C version from FFmpeg ALAC encoder/decoder
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

#ifndef AVCODEC_ALAC_DATA_TABLEGEN_CONSTEXPR_HPP
#define AVCODEC_ALAC_DATA_TABLEGEN_CONSTEXPR_HPP

#include <array>
#include <cstdint>

namespace FFmpegALACData {

// ============================================================================
// Constants
// ============================================================================

constexpr int ALAC_MAX_CHANNELS = 8;  // Maximum supported channels

// ============================================================================
// ALAC Raw Data Block Type Enum
// ============================================================================

/**
 * ALAC (Apple Lossless Audio Codec) raw data block types.
 *
 * These block types describe the structure of audio channel elements
 * in the ALAC bitstream. Different channel configurations use different
 * combinations of these element types.
 *
 * - SCE (Single Channel Element): Mono channel
 * - CPE (Channel Pair Element): Stereo pair (L/R)
 * - CCE (Coupling Channel Element): Additional channel for coupling
 * - LFE (Low Frequency Effect): Subwoofer channel
 * - DSE (Data Stream Element): Ancillary data
 * - PCE (Program Config Element): Channel configuration
 * - FIL (Fill Element): Padding/unused
 * - END (End): Marks end of frame
 */
enum class AlacRawDataBlockType : uint8_t {
    TYPE_SCE = 0,  // Single Channel Element (mono)
    TYPE_CPE = 1,  // Channel Pair Element (stereo)
    TYPE_CCE = 2,  // Coupling Channel Element
    TYPE_LFE = 3,  // Low Frequency Effect (subwoofer)
    TYPE_DSE = 4,  // Data Stream Element
    TYPE_PCE = 5,  // Program Config Element
    TYPE_FIL = 6,  // Fill Element
    TYPE_END = 7   // End marker
};

// ============================================================================
// ALAC Channel Layout Offset Table (64 bytes)
// ============================================================================

/**
 * Generate ALAC channel layout offset table.
 *
 * This table defines the remapping of audio channels from their logical
 * positions to their physical positions in the ALAC bitstream. Different
 * multi-channel configurations require different channel orderings.
 *
 * For each supported channel count (1-8), provides the output position
 * for each input channel. Used to reorder channels to match the ALAC
 * spec's required channel layout.
 *
 * Examples:
 * - 1 channel (mono): [0] - no reordering
 * - 2 channels (stereo): [0, 1] - L, R
 * - 3 channels (3.0): [2, 0, 1] - C, L, R → L, R, C
 * - 6 channels (5.1): [2, 0, 1, 4, 5, 3] - C, L, R, LS, RS, LFE → L, R, C, LFE, LS, RS
 *
 * The remapping ensures compatibility with Apple's channel order specification.
 */
constexpr auto generate_alac_channel_layout_offsets() noexcept {
    std::array<std::array<uint8_t, ALAC_MAX_CHANNELS>, ALAC_MAX_CHANNELS> table{};

    // 1 channel (mono)
    table[0][0] = 0;

    // 2 channels (stereo): L, R
    table[1][0] = 0;
    table[1][1] = 1;

    // 3 channels (3.0): C, L, R → remapped to L, R, C
    table[2][0] = 2;  // Input C → output position 2
    table[2][1] = 0;  // Input L → output position 0
    table[2][2] = 1;  // Input R → output position 1

    // 4 channels (4.0): C, L, R, Cs → remapped to L, R, C, Cs
    table[3][0] = 2;  // C → 2
    table[3][1] = 0;  // L → 0
    table[3][2] = 1;  // R → 1
    table[3][3] = 3;  // Cs → 3

    // 5 channels (5.0): C, L, R, Ls, Rs → remapped to L, R, C, Ls, Rs
    table[4][0] = 2;  // C → 2
    table[4][1] = 0;  // L → 0
    table[4][2] = 1;  // R → 1
    table[4][3] = 3;  // Ls → 3
    table[4][4] = 4;  // Rs → 4

    // 6 channels (5.1): C, L, R, Ls, Rs, LFE → remapped to L, R, C, LFE, Ls, Rs
    table[5][0] = 2;  // C → 2
    table[5][1] = 0;  // L → 0
    table[5][2] = 1;  // R → 1
    table[5][3] = 4;  // Ls → 4
    table[5][4] = 5;  // Rs → 5
    table[5][5] = 3;  // LFE → 3

    // 7 channels (6.1): C, L, R, Ls, Rs, Cs, LFE → remapped
    table[6][0] = 2;  // C → 2
    table[6][1] = 0;  // L → 0
    table[6][2] = 1;  // R → 1
    table[6][3] = 4;  // Ls → 4
    table[6][4] = 5;  // Rs → 5
    table[6][5] = 6;  // Cs → 6
    table[6][6] = 3;  // LFE → 3

    // 8 channels (7.1 wide back): C, Lc, Rc, L, R, Ls, Rs, LFE → remapped
    table[7][0] = 2;  // C → 2
    table[7][1] = 6;  // Lc → 6
    table[7][2] = 7;  // Rc → 7
    table[7][3] = 0;  // L → 0
    table[7][4] = 1;  // R → 1
    table[7][5] = 4;  // Ls → 4
    table[7][6] = 5;  // Rs → 5
    table[7][7] = 3;  // LFE → 3

    return table;
}

constexpr auto alac_channel_layout_offsets = generate_alac_channel_layout_offsets();

// ============================================================================
// ALAC Channel Elements Table (40 bytes)
// ============================================================================

/**
 * Generate ALAC channel elements configuration table.
 *
 * This table defines which raw data block types (SCE/CPE/LFE) are used
 * for each supported channel configuration. ALAC encodes audio in terms
 * of these element types rather than individual channels.
 *
 * Element types:
 * - SCE (Single Channel Element): One mono channel
 * - CPE (Channel Pair Element): Two channels (stereo pair)
 * - LFE (Low Frequency Effect): Subwoofer channel (not used in ALAC currently)
 *
 * Examples:
 * - 1 channel: [SCE] - single mono channel
 * - 2 channels: [CPE] - one stereo pair
 * - 3 channels: [SCE, CPE] - center + stereo pair
 * - 6 channels (5.1): [SCE, CPE, CPE, SCE] - center + L/R pair + LS/RS pair + LFE
 *
 * Each entry can have up to 5 elements (though most use fewer).
 */
constexpr auto generate_alac_channel_elements() noexcept {
    using Element = AlacRawDataBlockType;
    constexpr auto SCE = Element::TYPE_SCE;
    constexpr auto CPE = Element::TYPE_CPE;
    constexpr auto END = Element::TYPE_END;

    std::array<std::array<Element, 5>, ALAC_MAX_CHANNELS> table{};

    // Initialize all to END markers
    for (auto& row : table) {
        for (auto& elem : row) {
            elem = END;
        }
    }

    // 1 channel (mono): SCE
    table[0][0] = SCE;

    // 2 channels (stereo): CPE
    table[1][0] = CPE;

    // 3 channels (3.0): SCE (C), CPE (L/R)
    table[2][0] = SCE;
    table[2][1] = CPE;

    // 4 channels (4.0): SCE (C), CPE (L/R), SCE (Cs)
    table[3][0] = SCE;
    table[3][1] = CPE;
    table[3][2] = SCE;

    // 5 channels (5.0): SCE (C), CPE (L/R), CPE (Ls/Rs)
    table[4][0] = SCE;
    table[4][1] = CPE;
    table[4][2] = CPE;

    // 6 channels (5.1): SCE (C), CPE (L/R), CPE (Ls/Rs), SCE (LFE)
    table[5][0] = SCE;
    table[5][1] = CPE;
    table[5][2] = CPE;
    table[5][3] = SCE;

    // 7 channels (6.1): SCE (C), CPE (L/R), CPE (Ls/Rs), SCE (Cs), SCE (LFE)
    table[6][0] = SCE;
    table[6][1] = CPE;
    table[6][2] = CPE;
    table[6][3] = SCE;
    table[6][4] = SCE;

    // 8 channels (7.1): SCE (C), CPE (Lc/Rc), CPE (L/R), CPE (Ls/Rs), SCE (LFE)
    table[7][0] = SCE;
    table[7][1] = CPE;
    table[7][2] = CPE;
    table[7][3] = CPE;
    table[7][4] = SCE;

    return table;
}

constexpr auto alac_channel_elements = generate_alac_channel_elements();

// ============================================================================
// Static Assertions: Comprehensive Validation
// ============================================================================

// Table sizes
static_assert(alac_channel_layout_offsets.size() == ALAC_MAX_CHANNELS,
              "Channel layout offsets table has 8 rows");
static_assert(alac_channel_layout_offsets[0].size() == ALAC_MAX_CHANNELS,
              "Each row has 8 columns");
static_assert(alac_channel_elements.size() == ALAC_MAX_CHANNELS,
              "Channel elements table has 8 rows");
static_assert(alac_channel_elements[0].size() == 5,
              "Each row has 5 element slots");

// Mono configuration (1 channel)
static_assert(alac_channel_layout_offsets[0][0] == 0,
              "Mono: channel 0 → position 0");
static_assert(alac_channel_elements[0][0] == AlacRawDataBlockType::TYPE_SCE,
              "Mono: uses SCE element");

// Stereo configuration (2 channels)
static_assert(alac_channel_layout_offsets[1][0] == 0,
              "Stereo: left channel → position 0");
static_assert(alac_channel_layout_offsets[1][1] == 1,
              "Stereo: right channel → position 1");
static_assert(alac_channel_elements[1][0] == AlacRawDataBlockType::TYPE_CPE,
              "Stereo: uses CPE element");

// 3.0 configuration (3 channels)
static_assert(alac_channel_layout_offsets[2][0] == 2,
              "3.0: center → position 2");
static_assert(alac_channel_layout_offsets[2][1] == 0,
              "3.0: left → position 0");
static_assert(alac_channel_layout_offsets[2][2] == 1,
              "3.0: right → position 1");
static_assert(alac_channel_elements[2][0] == AlacRawDataBlockType::TYPE_SCE,
              "3.0: first element is SCE (center)");
static_assert(alac_channel_elements[2][1] == AlacRawDataBlockType::TYPE_CPE,
              "3.0: second element is CPE (L/R)");

// 5.1 configuration (6 channels) - most common surround
static_assert(alac_channel_layout_offsets[5][0] == 2,
              "5.1: center → position 2");
static_assert(alac_channel_layout_offsets[5][1] == 0,
              "5.1: left → position 0");
static_assert(alac_channel_layout_offsets[5][2] == 1,
              "5.1: right → position 1");
static_assert(alac_channel_layout_offsets[5][3] == 4,
              "5.1: left surround → position 4");
static_assert(alac_channel_layout_offsets[5][4] == 5,
              "5.1: right surround → position 5");
static_assert(alac_channel_layout_offsets[5][5] == 3,
              "5.1: LFE → position 3");
static_assert(alac_channel_elements[5][0] == AlacRawDataBlockType::TYPE_SCE,
              "5.1: SCE for center");
static_assert(alac_channel_elements[5][1] == AlacRawDataBlockType::TYPE_CPE,
              "5.1: CPE for L/R");
static_assert(alac_channel_elements[5][2] == AlacRawDataBlockType::TYPE_CPE,
              "5.1: CPE for LS/RS");
static_assert(alac_channel_elements[5][3] == AlacRawDataBlockType::TYPE_SCE,
              "5.1: SCE for LFE");

// 7.1 configuration (8 channels) - maximum
static_assert(alac_channel_layout_offsets[7][0] == 2,
              "7.1: center → position 2");
static_assert(alac_channel_layout_offsets[7][3] == 0,
              "7.1: left → position 0");
static_assert(alac_channel_layout_offsets[7][4] == 1,
              "7.1: right → position 1");
static_assert(alac_channel_elements[7][0] == AlacRawDataBlockType::TYPE_SCE,
              "7.1: SCE for center");
static_assert(alac_channel_elements[7][1] == AlacRawDataBlockType::TYPE_CPE,
              "7.1: CPE for center L/R");
static_assert(alac_channel_elements[7][2] == AlacRawDataBlockType::TYPE_CPE,
              "7.1: CPE for L/R");
static_assert(alac_channel_elements[7][3] == AlacRawDataBlockType::TYPE_CPE,
              "7.1: CPE for LS/RS");

// Verify offset values are within valid range
constexpr bool verify_offsets_in_range() {
    for (int ch = 0; ch < ALAC_MAX_CHANNELS; ++ch) {
        for (int i = 0; i <= ch; ++i) {  // Only check valid channels for this config
            if (alac_channel_layout_offsets[ch][i] >= ALAC_MAX_CHANNELS) {
                return false;
            }
        }
    }
    return true;
}
static_assert(verify_offsets_in_range(), "All channel offsets are within valid range");

// Verify no duplicate offsets within each configuration
constexpr bool verify_no_duplicate_offsets() {
    for (int ch = 0; ch < ALAC_MAX_CHANNELS; ++ch) {
        int num_channels = ch + 1;
        for (int i = 0; i < num_channels; ++i) {
            for (int j = i + 1; j < num_channels; ++j) {
                if (alac_channel_layout_offsets[ch][i] == alac_channel_layout_offsets[ch][j]) {
                    return false;  // Found duplicate
                }
            }
        }
    }
    return true;
}
static_assert(verify_no_duplicate_offsets(), "No duplicate offsets within each configuration");

// Verify channel elements are valid types
constexpr bool verify_element_types() {
    for (int ch = 0; ch < ALAC_MAX_CHANNELS; ++ch) {
        for (int i = 0; i < 5; ++i) {
            auto elem = alac_channel_elements[ch][i];
            // Valid types: SCE, CPE, END (LFE not currently used in ALAC)
            if (elem != AlacRawDataBlockType::TYPE_SCE &&
                elem != AlacRawDataBlockType::TYPE_CPE &&
                elem != AlacRawDataBlockType::TYPE_END) {
                return false;
            }
        }
    }
    return true;
}
static_assert(verify_element_types(), "All channel elements are valid types (SCE/CPE/END)");

// Verify element counts make sense for channel configurations
// CPE provides 2 channels, SCE provides 1 channel
constexpr bool verify_element_channel_counts() {
    for (int ch = 0; ch < ALAC_MAX_CHANNELS; ++ch) {
        int expected_channels = ch + 1;
        int counted_channels = 0;

        for (int i = 0; i < 5; ++i) {
            auto elem = alac_channel_elements[ch][i];
            if (elem == AlacRawDataBlockType::TYPE_SCE) {
                counted_channels += 1;
            } else if (elem == AlacRawDataBlockType::TYPE_CPE) {
                counted_channels += 2;
            } else if (elem == AlacRawDataBlockType::TYPE_END) {
                break;  // Rest are END markers
            }
        }

        if (counted_channels != expected_channels) {
            return false;
        }
    }
    return true;
}
static_assert(verify_element_channel_counts(),
              "Element types provide correct number of channels for each configuration");

} // namespace FFmpegALACData

// ============================================================================
// C API Compatibility
// ============================================================================

extern "C" {

/**
 * Get pointer to ALAC channel layout offset table.
 * Returns: Pointer to 8×8 uint8_t array
 */
inline const uint8_t (*get_alac_channel_layout_offsets())[8] {
    return reinterpret_cast<const uint8_t(*)[8]>(
        FFmpegALACData::alac_channel_layout_offsets.data()
    );
}

} // extern "C"

#endif // AVCODEC_ALAC_DATA_TABLEGEN_CONSTEXPR_HPP
