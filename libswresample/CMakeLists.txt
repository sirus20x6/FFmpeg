# libswresample - FFmpeg resample library

set(SWRESAMPLE_SOURCES
    audioconvert.c
    dither.c
    rematrix.c
    resample.c
    resample_dsp.c
    swresample.c
)

set(SWRESAMPLE_HEADERS
    swresample.h
    version.h
    version_major.h
)

add_library(swresample ${SWRESAMPLE_SOURCES})

set_target_properties(swresample PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    PUBLIC_HEADER "${SWRESAMPLE_HEADERS}"
    OUTPUT_NAME "swresample"
)

target_include_directories(swresample
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/..>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(swresample
    PUBLIC
        avutil
)

target_compile_definitions(swresample PRIVATE
    HAVE_AV_CONFIG_H
)

if(BUILD_SHARED_LIBS)
    target_compile_definitions(swresample PRIVATE BUILDING_swresample)
endif()

install(TARGETS swresample
    EXPORT FFmpegTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    PUBLIC_HEADER DESTINATION include/libswresample
)
