/*
 * Modern C++ constexpr pixel format utilities
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
 * Modern C++20 constexpr pixel format utilities
 *
 * Provides compile-time computation of pixel format properties such as:
 * - Bits per pixel
 * - Number of planes
 * - Chroma subsampling factors
 * - Buffer size calculations
 *
 * These can be computed at compile time when pixel format is known statically,
 * improving performance and enabling compile-time validation.
 */

#ifndef AVUTIL_PIXFMT_CONSTEXPR_HPP
#define AVUTIL_PIXFMT_CONSTEXPR_HPP

#include <cstdint>

extern "C" {
#include "pixfmt.h"
}

namespace ffmpeg {
namespace pixfmt {

/**
 * Pixel format traits - compile-time properties
 */
struct PixelFormatTraits {
    int bits_per_pixel;
    int num_planes;
    int h_chroma_subsample;  // log2
    int v_chroma_subsample;  // log2
    bool has_alpha;
    bool is_planar;
    bool is_rgb;
};

/**
 * Get compile-time traits for common pixel formats
 * This allows compile-time optimizations based on format
 */
constexpr PixelFormatTraits get_traits(AVPixelFormat fmt) noexcept {
    switch (fmt) {
        case AV_PIX_FMT_YUV420P:
            return {12, 3, 1, 1, false, true, false};
        case AV_PIX_FMT_YUV422P:
            return {16, 3, 1, 0, false, true, false};
        case AV_PIX_FMT_YUV444P:
            return {24, 3, 0, 0, false, true, false};
        case AV_PIX_FMT_YUYV422:
            return {16, 1, 1, 0, false, false, false};
        case AV_PIX_FMT_UYVY422:
            return {16, 1, 1, 0, false, false, false};
        case AV_PIX_FMT_RGB24:
            return {24, 1, 0, 0, false, false, true};
        case AV_PIX_FMT_BGR24:
            return {24, 1, 0, 0, false, false, true};
        case AV_PIX_FMT_RGBA:
            return {32, 1, 0, 0, true, false, true};
        case AV_PIX_FMT_BGRA:
            return {32, 1, 0, 0, true, false, true};
        case AV_PIX_FMT_ARGB:
            return {32, 1, 0, 0, true, false, true};
        case AV_PIX_FMT_ABGR:
            return {32, 1, 0, 0, true, false, true};
        case AV_PIX_FMT_GRAY8:
            return {8, 1, 0, 0, false, true, false};
        case AV_PIX_FMT_GRAY16LE:
        case AV_PIX_FMT_GRAY16BE:
            return {16, 1, 0, 0, false, true, false};
        case AV_PIX_FMT_YUV420P10LE:
        case AV_PIX_FMT_YUV420P10BE:
            return {15, 3, 1, 1, false, true, false};
        case AV_PIX_FMT_YUV420P16LE:
        case AV_PIX_FMT_YUV420P16BE:
            return {24, 3, 1, 1, false, true, false};
        case AV_PIX_FMT_NV12:
            return {12, 2, 1, 1, false, true, false};
        case AV_PIX_FMT_NV21:
            return {12, 2, 1, 1, false, true, false};
        default:
            return {0, 0, 0, 0, false, false, false};
    }
}

/**
 * Compile-time check if format is YUV
 */
constexpr bool is_yuv(AVPixelFormat fmt) noexcept {
    auto traits = get_traits(fmt);
    return !traits.is_rgb && traits.num_planes > 0;
}

/**
 * Compile-time check if format is RGB
 */
constexpr bool is_rgb(AVPixelFormat fmt) noexcept {
    return get_traits(fmt).is_rgb;
}

/**
 * Compile-time check if format has alpha channel
 */
constexpr bool has_alpha(AVPixelFormat fmt) noexcept {
    return get_traits(fmt).has_alpha;
}

/**
 * Compile-time check if format is planar
 */
constexpr bool is_planar(AVPixelFormat fmt) noexcept {
    return get_traits(fmt).is_planar;
}

/**
 * Compile-time bits per pixel calculation
 */
constexpr int bits_per_pixel(AVPixelFormat fmt) noexcept {
    return get_traits(fmt).bits_per_pixel;
}

/**
 * Compile-time bytes per pixel calculation (rounded up)
 */
constexpr int bytes_per_pixel(AVPixelFormat fmt) noexcept {
    return (bits_per_pixel(fmt) + 7) / 8;
}

/**
 * Compile-time plane count
 */
constexpr int num_planes(AVPixelFormat fmt) noexcept {
    return get_traits(fmt).num_planes;
}

/**
 * Compile-time chroma width calculation
 * Returns width of chroma plane given luma width
 */
constexpr int chroma_width(int luma_width, AVPixelFormat fmt) noexcept {
    auto traits = get_traits(fmt);
    return luma_width >> traits.h_chroma_subsample;
}

/**
 * Compile-time chroma height calculation
 * Returns height of chroma plane given luma height
 */
constexpr int chroma_height(int luma_height, AVPixelFormat fmt) noexcept {
    auto traits = get_traits(fmt);
    return luma_height >> traits.v_chroma_subsample;
}

/**
 * Compile-time buffer size calculation for a single plane
 * Assumes no padding/stride
 */
constexpr int64_t plane_buffer_size(int width, int height, int bytes_per_pixel) noexcept {
    return static_cast<int64_t>(width) * height * bytes_per_pixel;
}

/**
 * Compile-time total buffer size calculation
 * Returns minimum buffer size needed for given dimensions and format
 * This is a simplified version; runtime version should account for alignment
 */
constexpr int64_t image_buffer_size(int width, int height, AVPixelFormat fmt) noexcept {
    auto traits = get_traits(fmt);

    if (!traits.is_planar) {
        // Packed format
        return plane_buffer_size(width, height, bytes_per_pixel(fmt));
    }

    // Planar format - need to sum all planes
    int64_t total = 0;

    // Luma plane
    total += plane_buffer_size(width, height, 1);

    // Chroma planes (if any)
    if (traits.num_planes > 1) {
        int chroma_w = chroma_width(width, fmt);
        int chroma_h = chroma_height(height, fmt);
        int chroma_planes = traits.num_planes - 1;

        total += plane_buffer_size(chroma_w, chroma_h, 1) * chroma_planes;
    }

    return total;
}

/**
 * Compile-time check if two formats have compatible chroma subsampling
 */
constexpr bool compatible_subsampling(AVPixelFormat fmt1, AVPixelFormat fmt2) noexcept {
    auto t1 = get_traits(fmt1);
    auto t2 = get_traits(fmt2);

    return t1.h_chroma_subsample == t2.h_chroma_subsample &&
           t1.v_chroma_subsample == t2.v_chroma_subsample;
}

/**
 * Compile-time check if format conversion requires chroma resampling
 */
constexpr bool needs_chroma_resampling(AVPixelFormat src, AVPixelFormat dst) noexcept {
    return !compatible_subsampling(src, dst);
}

/**
 * Common format groups as constexpr arrays
 */
namespace format_groups {
    // Common 4:2:0 formats
    constexpr AVPixelFormat yuv420_formats[] = {
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUV420P10LE,
        AV_PIX_FMT_YUV420P10BE,
        AV_PIX_FMT_YUV420P16LE,
        AV_PIX_FMT_YUV420P16BE,
        AV_PIX_FMT_NV12,
        AV_PIX_FMT_NV21,
    };

    // Common RGB formats
    constexpr AVPixelFormat rgb_formats[] = {
        AV_PIX_FMT_RGB24,
        AV_PIX_FMT_BGR24,
        AV_PIX_FMT_RGBA,
        AV_PIX_FMT_BGRA,
        AV_PIX_FMT_ARGB,
        AV_PIX_FMT_ABGR,
    };

    // Packed YUV formats
    constexpr AVPixelFormat packed_yuv_formats[] = {
        AV_PIX_FMT_YUYV422,
        AV_PIX_FMT_UYVY422,
    };
}

// Example compile-time calculations
namespace examples {
    // Buffer size for 1080p YUV420
    constexpr int64_t buffer_1080p_yuv420 =
        image_buffer_size(1920, 1080, AV_PIX_FMT_YUV420P);

    // Buffer size for 4K RGBA
    constexpr int64_t buffer_4k_rgba =
        image_buffer_size(3840, 2160, AV_PIX_FMT_RGBA);

    // Check if NV12 and YUV420P have compatible subsampling
    constexpr bool nv12_yuv420p_compatible =
        compatible_subsampling(AV_PIX_FMT_NV12, AV_PIX_FMT_YUV420P);

    static_assert(nv12_yuv420p_compatible, "NV12 and YUV420P should be compatible");
}

// Static assertions to validate implementations
static_assert(bits_per_pixel(AV_PIX_FMT_YUV420P) == 12, "YUV420P bpp failed");
static_assert(bits_per_pixel(AV_PIX_FMT_RGB24) == 24, "RGB24 bpp failed");
static_assert(bits_per_pixel(AV_PIX_FMT_RGBA) == 32, "RGBA bpp failed");
static_assert(num_planes(AV_PIX_FMT_YUV420P) == 3, "YUV420P planes failed");
static_assert(num_planes(AV_PIX_FMT_NV12) == 2, "NV12 planes failed");
static_assert(num_planes(AV_PIX_FMT_RGB24) == 1, "RGB24 planes failed");
static_assert(has_alpha(AV_PIX_FMT_RGBA), "RGBA alpha check failed");
static_assert(!has_alpha(AV_PIX_FMT_RGB24), "RGB24 alpha check failed");
static_assert(is_planar(AV_PIX_FMT_YUV420P), "YUV420P planar check failed");
static_assert(!is_planar(AV_PIX_FMT_YUYV422), "YUYV422 planar check failed");
static_assert(chroma_width(1920, AV_PIX_FMT_YUV420P) == 960, "Chroma width failed");
static_assert(chroma_height(1080, AV_PIX_FMT_YUV420P) == 540, "Chroma height failed");

} // namespace pixfmt
} // namespace ffmpeg

#endif // AVUTIL_PIXFMT_CONSTEXPR_HPP
