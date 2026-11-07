find_package(GTest CONFIG REQUIRED)
find_package(prometheus-cpp CONFIG REQUIRED)

add_executable(mwcapture-test test.cpp conversion_test.cpp metrics_test.cpp)

# mwcapture is a MODULE (.ax COM filter, no import lib to link against);
# test.cpp only needs its header-only mw_domain.h (included via a relative
# path) plus the Magewell SDK headers it declares types from.
add_dependencies(mwcapture-test mwcapture)

target_include_directories(mwcapture-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../mwsdk/include
)

target_link_libraries(mwcapture-test PRIVATE
    directshow_baseclasses
    common
    GTest::gtest
    prometheus-cpp::pull
)

target_compile_definitions(mwcapture-test PRIVATE
    X64
    _CONSOLE
    $<$<CONFIG:Debug>:_DEBUG>
    # match common so both see the same logging.h
    $<$<AND:$<CONFIG:Release>,$<NOT:$<BOOL:${EZ_ENABLE_TRACE}>>>:NO_LOGGING>
    $<$<CONFIG:Release>:NDEBUG>
)

ez_target_common(mwcapture-test)

enable_testing()
add_test(NAME mwcapture-test COMMAND mwcapture-test)
