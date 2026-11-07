find_package(quill CONFIG REQUIRED)

add_library(bmcapture MODULE
    any_rgb.cpp
    bm_audio_capture_filter.cpp
    bm_capture_filter.cpp
    bm_video_capture_pin.cpp
    dllmain.cpp
    MediaSampleBackedDecklinkBuffer.cpp
    bmcapture.rc
    bmcapture.def
)

set_target_properties(bmcapture PROPERTIES
    SUFFIX ".ax"
    PREFIX ""
)

target_link_libraries(bmcapture PRIVATE
    directshow_baseclasses
    common
    DeckLink
    quill::quill
    Comctl32.lib
    strmiids.lib
    winmm.lib
    ws2_32.lib
)

target_compile_definitions(bmcapture PRIVATE
    _CRT_SECURE_NO_WARNINGS
    BMCAPTURE_EXPORTS
    _WINDOWS
    _USRDLL
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Debug>:RECORD_RAW>
    $<$<CONFIG:Release>:NDEBUG>
    $<$<AND:$<CONFIG:Release>,$<NOT:$<BOOL:${EZ_ENABLE_TRACE}>>>:NO_LOGGING>
    $<$<AND:$<CONFIG:Release>,$<BOOL:${EZ_ENABLE_TRACE}>>:CAPTURE_NAME_SUFFIX=1>
)

ez_target_common(bmcapture)
