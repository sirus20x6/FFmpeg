# libavfilter - FFmpeg filter library

set(AVFILTER_SOURCES
    allfilters.c
    avfilter.c
    avfiltergraph.c
    buffersink.c
    buffersrc.c
    formats.c
    graphparser.c
    video.c
)

# C++ sources for special backends
set(AVFILTER_CXX_SOURCES
    dnn/dnn_backend_torch.cpp
    vsrc_gfxcapture_winrt.cpp
)

set(AVFILTER_HEADERS
    avfilter.h
    buffersink.h
    buffersrc.h
    version.h
    version_major.h
)

add_library(avfilter ${AVFILTER_SOURCES})

# Add C++ sources only if their dependencies are available
# For now, we'll conditionally exclude them
# list(APPEND AVFILTER_SOURCES ${AVFILTER_CXX_SOURCES})

set_target_properties(avfilter PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    PUBLIC_HEADER "${AVFILTER_HEADERS}"
    OUTPUT_NAME "avfilter"
)

target_include_directories(avfilter
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/..>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(avfilter
    PUBLIC
        avcodec
        avformat
        avutil
    PRIVATE
        m
)

target_compile_definitions(avfilter PRIVATE
    HAVE_AV_CONFIG_H
)

if(BUILD_SHARED_LIBS)
    target_compile_definitions(avfilter PRIVATE BUILDING_avfilter)
endif()

install(TARGETS avfilter
    EXPORT FFmpegTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    PUBLIC_HEADER DESTINATION include/libavfilter
)
