# libavutil - FFmpeg utility library

set(AVUTIL_HEADERS
    adler32.h
    aes.h
    aes_ctr.h
    ambient_viewing_environment.h
    attributes.h
    audio_fifo.h
    avassert.h
    avstring.h
    avutil.h
    base64.h
    blowfish.h
    bprint.h
    bswap.h
    buffer.h
    cast5.h
    camellia.h
    channel_layout.h
    common.h
    container_fifo.h
    cpu.h
    crc.h
    csp.h
    des.h
    detection_bbox.h
    dict.h
    display.h
    dovi_meta.h
    downmix_info.h
    encryption_info.h
    error.h
    eval.h
    executor.h
    fifo.h
    file.h
    film_grain_params.h
    frame.h
    hash.h
    hdr_dynamic_metadata.h
    hdr_dynamic_vivid_metadata.h
    hmac.h
    hwcontext.h
    iamf.h
    imgutils.h
    intfloat.h
    intreadwrite.h
    lfg.h
    log.h
    lzo.h
    macros.h
    mathematics.h
    mastering_display_metadata.h
    md5.h
    mem.h
    motion_vector.h
    murmur3.h
    opt.h
    parseutils.h
    pixdesc.h
    pixelutils.h
    pixfmt.h
    random_seed.h
    rc4.h
    rational.h
    refstruct.h
    replaygain.h
    ripemd.h
    samplefmt.h
    sha.h
    sha512.h
    spherical.h
    stereo3d.h
    tdrdi.h
    threadmessage.h
    time.h
    timecode.h
    timestamp.h
    tree.h
    twofish.h
    uuid.h
    version.h
    video_enc_params.h
    xtea.h
    tea.h
    tx.h
    video_hint.h
)

set(AVUTIL_SOURCES
    adler32.c
    aes.c
    aes_ctr.c
    ambient_viewing_environment.c
    audio_fifo.c
    avstring.c
    avsscanf.c
    base64.c
    blowfish.c
    bprint.c
    buffer.c
    cast5.c
    camellia.c
    channel_layout.c
    container_fifo.c
    cpu.c
    crc.c
    csp.c
    des.c
    detection_bbox.c
    dict.c
    display.c
    dovi_meta.c
    downmix_info.c
    encryption_info.c
    error.c
    eval.c
    executor.c
    fifo.c
    file.c
    file_open.c
    float_dsp.c
    float_scalarproduct.c
    film_grain_params.c
    fixed_dsp.c
    frame.c
    hash.c
    hdr_dynamic_metadata.c
    hdr_dynamic_vivid_metadata.c
    hmac.c
    hwcontext.c
    iamf.c
    imgutils.c
    integer.c
    intmath.c
    lfg.c
    lls.c
    log.c
    log2_tab.c
    lzo.c
    mathematics.c
    mastering_display_metadata.c
    md5.c
    mem.c
    murmur3.c
    opt.c
    parseutils.c
    pixdesc.c
    pixelutils.c
    random_seed.c
    rational.c
    refstruct.c
    reverse.c
    rc4.c
    ripemd.c
    samplefmt.c
    side_data.c
    sha.c
    sha512.c
    slicethread.c
    spherical.c
    stereo3d.c
    tdrdi.c
    threadmessage.c
    time.c
    timecode.c
    timecode_internal.c
    timestamp.c
    tree.c
    twofish.c
    utils.c
    xga_font_data.c
    xtea.c
    tea.c
    tx.c
    tx_float.c
    tx_double.c
    tx_int32.c
    uuid.c
    version.c
    video_enc_params.c
    video_hint.c
)

# Conditional sources based on hardware acceleration support
if(CONFIG_CUDA)
    list(APPEND AVUTIL_SOURCES hwcontext_cuda.c)
    list(APPEND AVUTIL_HEADERS hwcontext_cuda.h)
endif()

if(CONFIG_VULKAN)
    list(APPEND AVUTIL_SOURCES hwcontext_vulkan.c vulkan.c)
    list(APPEND AVUTIL_HEADERS hwcontext_vulkan.h)
endif()

if(CONFIG_VAAPI)
    list(APPEND AVUTIL_SOURCES hwcontext_vaapi.c)
    list(APPEND AVUTIL_HEADERS hwcontext_vaapi.h)
endif()

if(CONFIG_VDPAU)
    list(APPEND AVUTIL_SOURCES hwcontext_vdpau.c)
    list(APPEND AVUTIL_HEADERS hwcontext_vdpau.h)
endif()

if(CONFIG_D3D11VA)
    list(APPEND AVUTIL_SOURCES hwcontext_d3d11va.c)
    list(APPEND AVUTIL_HEADERS hwcontext_d3d11va.h)
endif()

if(CONFIG_D3D12VA)
    list(APPEND AVUTIL_SOURCES hwcontext_d3d12va.c)
    list(APPEND AVUTIL_HEADERS hwcontext_d3d12va.h)
endif()

if(NOT CONFIG_VULKAN)
    list(APPEND AVUTIL_SOURCES hwcontext_stub.c)
endif()

# Create the library
add_library(avutil ${AVUTIL_SOURCES})

# Set library properties
set_target_properties(avutil PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    PUBLIC_HEADER "${AVUTIL_HEADERS}"
    OUTPUT_NAME "avutil"
)

# Include directories
target_include_directories(avutil
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/..>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link libraries
find_package(Threads REQUIRED)
target_link_libraries(avutil
    PUBLIC
        Threads::Threads
        ${CMAKE_DL_LIBS}
    PRIVATE
        m
)

# Compile definitions
target_compile_definitions(avutil PRIVATE
    HAVE_AV_CONFIG_H
)

if(BUILD_SHARED_LIBS)
    target_compile_definitions(avutil PRIVATE BUILDING_avutil)
endif()

# Install rules
install(TARGETS avutil
    EXPORT FFmpegTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    PUBLIC_HEADER DESTINATION include/libavutil
)
