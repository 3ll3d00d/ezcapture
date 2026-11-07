find_package(quill CONFIG REQUIRED)

add_library(LibMWCapture STATIC IMPORTED GLOBAL)
set_target_properties(LibMWCapture PROPERTIES
    IMPORTED_LOCATION_DEBUG   ${CMAKE_CURRENT_SOURCE_DIR}/../mwsdk/lib/x64/Debug/LibMWCapture.lib
    IMPORTED_LOCATION_RELEASE ${CMAKE_CURRENT_SOURCE_DIR}/../mwsdk/lib/x64/Release/LibMWCapture.lib
)

add_library(mwcapture MODULE
    dllmain.cpp
    mw_audio_capture_pin.cpp
    mw_capture_filter.cpp
    mw_video_capture_pin.cpp
    mwcapture.rc
    mwcapture.def
)

set_target_properties(mwcapture PROPERTIES
    SUFFIX ".ax"
    PREFIX ""
)

target_include_directories(mwcapture PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../mwsdk/include
)

target_link_libraries(mwcapture PRIVATE
    directshow_baseclasses
    common
    LibMWCapture
    quill::quill
    Comctl32.lib
    strmiids.lib
    winmm.lib
    ws2_32.lib
)

target_compile_definitions(mwcapture PRIVATE
    _CRT_SECURE_NO_WARNINGS
    _USRDLL
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Debug>:_WINDOWS>
    $<$<CONFIG:Release>:NDEBUG>
    $<$<CONFIG:Release>:_WINSOCK_DEPRECATED_NO_WARNINGS>
    $<$<CONFIG:Release>:WIN32_LEAN_AND_MEAN>
    $<$<AND:$<CONFIG:Release>,$<NOT:$<BOOL:${EZ_ENABLE_TRACE}>>>:NO_LOGGING>
    $<$<AND:$<CONFIG:Release>,$<BOOL:${EZ_ENABLE_TRACE}>>:CAPTURE_NAME_SUFFIX=1>
)

ez_target_common(mwcapture)
