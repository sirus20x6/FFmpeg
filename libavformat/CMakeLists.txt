# libavformat - FFmpeg format library

set(AVFORMAT_SOURCES
    allformats.c
    avio.c
    aviobuf.c
    avformat.c
    format.c
    mux.c
    demux.c
    protocols.c
    url.c
)

set(AVFORMAT_HEADERS
    avformat.h
    avio.h
    version.h
    version_major.h
)

add_library(avformat ${AVFORMAT_SOURCES})

set_target_properties(avformat PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    PUBLIC_HEADER "${AVFORMAT_HEADERS}"
    OUTPUT_NAME "avformat"
)

target_include_directories(avformat
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/..>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(avformat
    PUBLIC
        avcodec
        avutil
)

target_compile_definitions(avformat PRIVATE
    HAVE_AV_CONFIG_H
)

if(BUILD_SHARED_LIBS)
    target_compile_definitions(avformat PRIVATE BUILDING_avformat)
endif()

install(TARGETS avformat
    EXPORT FFmpegTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    PUBLIC_HEADER DESTINATION include/libavformat
)
