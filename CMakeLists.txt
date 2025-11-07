cmake_minimum_required(VERSION 3.20)

# Project declaration
project(FFmpeg
    VERSION 7.1.0
    DESCRIPTION "FFmpeg multimedia framework - Modernized with CMake"
    LANGUAGES C CXX ASM
)

# Set C and C++ standards
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)  # GNU extensions for compatibility

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)  # Modern C++ without extensions

# Build options
option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(BUILD_PROGRAMS "Build ffmpeg, ffplay, ffprobe programs" ON)
option(BUILD_DOC "Build documentation" OFF)
option(BUILD_EXAMPLES "Build examples" OFF)

# Configuration options (subset of most common)
option(CONFIG_AVDEVICE "Enable AVDevice library" ON)
option(CONFIG_AVFILTER "Enable AVFilter library" ON)
option(CONFIG_AVFORMAT "Enable AVFormat library" ON)
option(CONFIG_AVCODEC "Enable AVCodec library" ON)
option(CONFIG_SWRESAMPLE "Enable SWResample library" ON)
option(CONFIG_SWSCALE "Enable SWScale library" ON)

# Hardware acceleration options
option(CONFIG_CUDA "Enable CUDA support" OFF)
option(CONFIG_VULKAN "Enable Vulkan support" OFF)
option(CONFIG_VAAPI "Enable VAAPI support" OFF)
option(CONFIG_VDPAU "Enable VDPAU support" OFF)
option(CONFIG_D3D11VA "Enable D3D11VA support" OFF)
option(CONFIG_D3D12VA "Enable D3D12VA support" OFF)

# Compiler flags
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall
        -Wextra
        -Wno-unused-parameter
        -Wno-sign-compare
        -Wno-pointer-sign
        -Wno-switch
        -fno-strict-aliasing
    )
endif()

# Position independent code for shared libraries
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Platform detection
if(WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0600)
elseif(UNIX AND NOT APPLE)
    add_compile_definitions(_GNU_SOURCE)
endif()

# Architecture detection
include(CheckCSourceCompiles)
include(CheckCXXSourceCompiles)
include(CheckIncludeFile)
include(CheckSymbolExists)

# Global include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Version information
set(FFMPEG_VERSION "${PROJECT_VERSION}")
# Note: Version file generation would go here
# For now, we rely on existing build system's generated files

# Library linking order (important!)
set(FFMPEG_LIBRARIES "")

# Build libraries in dependency order
add_subdirectory(libavutil)
list(APPEND FFMPEG_LIBRARIES avutil)

if(CONFIG_SWSCALE)
    add_subdirectory(libswscale)
    list(APPEND FFMPEG_LIBRARIES swscale)
endif()

if(CONFIG_SWRESAMPLE)
    add_subdirectory(libswresample)
    list(APPEND FFMPEG_LIBRARIES swresample)
endif()

if(CONFIG_AVCODEC)
    add_subdirectory(libavcodec)
    list(APPEND FFMPEG_LIBRARIES avcodec)
endif()

if(CONFIG_AVFORMAT)
    add_subdirectory(libavformat)
    list(APPEND FFMPEG_LIBRARIES avformat)
endif()

if(CONFIG_AVFILTER)
    add_subdirectory(libavfilter)
    list(APPEND FFMPEG_LIBRARIES avfilter)
endif()

if(CONFIG_AVDEVICE)
    add_subdirectory(libavdevice)
    list(APPEND FFMPEG_LIBRARIES avdevice)
endif()

# Build programs (TODO: implement fftools CMakeLists.txt)
# if(BUILD_PROGRAMS)
#     add_subdirectory(fftools)
# endif()

# Status message
message(STATUS "")
message(STATUS "FFmpeg Configuration Summary:")
message(STATUS "  Version:        ${FFMPEG_VERSION}")
message(STATUS "  C Standard:     C${CMAKE_C_STANDARD}")
message(STATUS "  C++ Standard:   C++${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "  Shared Libs:    ${BUILD_SHARED_LIBS}")
message(STATUS "")
message(STATUS "Components:")
message(STATUS "  AVUtil:         ON (required)")
message(STATUS "  AVCodec:        ${CONFIG_AVCODEC}")
message(STATUS "  AVFormat:       ${CONFIG_AVFORMAT}")
message(STATUS "  AVDevice:       ${CONFIG_AVDEVICE}")
message(STATUS "  AVFilter:       ${CONFIG_AVFILTER}")
message(STATUS "  SWResample:     ${CONFIG_SWRESAMPLE}")
message(STATUS "  SWScale:        ${CONFIG_SWSCALE}")
message(STATUS "")
