find_package(quill CONFIG REQUIRED)
find_package(prometheus-cpp CONFIG REQUIRED)

add_library(common STATIC
    audio_capture_pin.cpp
    capture_filter.cpp
    capture_pin.cpp
    metrics.cpp
    modeswitcher.cpp
    signalinfo.cpp
    video_capture_pin.cpp
)

target_include_directories(common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# quill is PUBLIC because logging.h/metrics.h include it; prometheus-cpp is only used by metrics.cpp
target_link_libraries(common PUBLIC directshow_baseclasses quill::quill PRIVATE prometheus-cpp::pull)

target_compile_definitions(common PRIVATE
    _CRT_SECURE_NO_WARNINGS
    _LIB
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    # Release/NoArch compile out logging (NO_LOGGING, quill stays for metrics);
    # Trace builds keep it and tag the binary name instead (CAPTURE_NAME_SUFFIX)
    # -- see EZ_ENABLE_TRACE.
    $<$<AND:$<CONFIG:Release>,$<NOT:$<BOOL:${EZ_ENABLE_TRACE}>>>:NO_LOGGING>
    $<$<AND:$<CONFIG:Release>,$<BOOL:${EZ_ENABLE_TRACE}>>:CAPTURE_NAME_SUFFIX=1>
    # non-empty rather than truthy, as in common.vcxproj, so a 0 major version is kept
    $<$<AND:$<CONFIG:Release>,$<NOT:$<STREQUAL:${EZ_VERSION_MAJOR},>>>:EZ_VERSION_MAJOR=${EZ_VERSION_MAJOR}>
    $<$<AND:$<CONFIG:Release>,$<NOT:$<STREQUAL:${EZ_VERSION_MINOR},>>>:EZ_VERSION_MINOR=${EZ_VERSION_MINOR}>
    $<$<AND:$<CONFIG:Release>,$<NOT:$<STREQUAL:${EZ_VERSION_PATCH},>>>:EZ_VERSION_PATCH=${EZ_VERSION_PATCH}>
    $<$<AND:$<CONFIG:Release>,$<NOT:$<STREQUAL:${EZ_VERSION_SUFFIX},>>>:EZ_VERSION_SUFFIX=${EZ_VERSION_SUFFIX}>
)

ez_target_common(common)
