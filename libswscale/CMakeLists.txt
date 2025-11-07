# libswscale - FFmpeg scale library

set(SWSCALE_SOURCES
    input.c
    output.c
    rgb2rgb.c
    swscale.c
    swscale_unscaled.c
    utils.c
    yuv2rgb.c
)

set(SWSCALE_HEADERS
    swscale.h
    version.h
    version_major.h
)

add_library(swscale ${SWSCALE_SOURCES})

set_target_properties(swscale PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    PUBLIC_HEADER "${SWSCALE_HEADERS}"
    OUTPUT_NAME "swscale"
)

target_include_directories(swscale
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/..>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(swscale
    PUBLIC
        avutil
)

target_compile_definitions(swscale PRIVATE
    HAVE_AV_CONFIG_H
)

if(BUILD_SHARED_LIBS)
    target_compile_definitions(swscale PRIVATE BUILDING_swscale)
endif()

install(TARGETS swscale
    EXPORT FFmpegTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    PUBLIC_HEADER DESTINATION include/libswscale
)
