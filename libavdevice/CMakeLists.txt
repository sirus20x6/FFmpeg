# libavdevice - FFmpeg device library

set(AVDEVICE_SOURCES
    alldevices.c
    avdevice.c
)

# C++ sources for Blackmagic DeckLink support
set(AVDEVICE_CXX_SOURCES
    decklink_common.cpp
    decklink_dec.cpp
    decklink_enc.cpp
)

set(AVDEVICE_HEADERS
    avdevice.h
    version.h
    version_major.h
)

add_library(avdevice ${AVDEVICE_SOURCES})

# Add C++ sources only if DeckLink support is needed
# For now, we'll conditionally exclude them
# list(APPEND AVDEVICE_SOURCES ${AVDEVICE_CXX_SOURCES})

set_target_properties(avdevice PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    PUBLIC_HEADER "${AVDEVICE_HEADERS}"
    OUTPUT_NAME "avdevice"
)

target_include_directories(avdevice
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/..>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(avdevice
    PUBLIC
        avcodec
        avformat
        avutil
)

target_compile_definitions(avdevice PRIVATE
    HAVE_AV_CONFIG_H
)

if(BUILD_SHARED_LIBS)
    target_compile_definitions(avdevice PRIVATE BUILDING_avdevice)
endif()

install(TARGETS avdevice
    EXPORT FFmpegTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    PUBLIC_HEADER DESTINATION include/libavdevice
)
