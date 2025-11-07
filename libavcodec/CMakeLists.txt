# libavcodec - FFmpeg codec library

# Read Makefile to get sources (simplified version - we'll include core sources)
set(AVCODEC_SOURCES
    ac3_parser.c
    adts_parser.c
    allcodecs.c
    avcodec.c
    avdct.c
    packet.c
    bitstream.c
    bitstream_filters.c
    bsf.c
    codec_desc.c
    codec_par.c
    d3d11va.c
    decode.c
    dirac.c
    dv_profile.c
    encode.c
    exif.c
    get_buffer.c
    imgconvert.c
    jni.c
    mathtables.cpp
    mediacodec.c
    mpeg12framerate.c
    options.c
    parser.c
    parsers.c
    profiles.c
    qsv_api.c
    raw.c
    threadprogress.c
    tiff_common.c
    utils.c
    version.c
    vlc.c
    vorbis_parser.c
    xiph.c
)

set(AVCODEC_HEADERS
    ac3_parser.h
    adts_parser.h
    avcodec.h
    avdct.h
    bsf.h
    codec.h
    codec_desc.h
    codec_id.h
    codec_par.h
    d3d11va.h
    defs.h
    dirac.h
    dv_profile.h
    dxva2.h
    exif.h
    jni.h
    mediacodec.h
    packet.h
    qsv.h
    smpte_436m.h
    vdpau.h
    version.h
    version_major.h
    videotoolbox.h
    vorbis_parser.h
)

# Note: In a full implementation, we would include all codec sources
# For this modernization proof-of-concept, we're including the core only

add_library(avcodec ${AVCODEC_SOURCES})

set_target_properties(avcodec PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    PUBLIC_HEADER "${AVCODEC_HEADERS}"
    OUTPUT_NAME "avcodec"
)

target_include_directories(avcodec
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/..>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(avcodec
    PUBLIC
        avutil
    PRIVATE
        m
)

target_compile_definitions(avcodec PRIVATE
    HAVE_AV_CONFIG_H
)

if(BUILD_SHARED_LIBS)
    target_compile_definitions(avcodec PRIVATE BUILDING_avcodec)
endif()

install(TARGETS avcodec
    EXPORT FFmpegTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    PUBLIC_HEADER DESTINATION include/libavcodec
)
