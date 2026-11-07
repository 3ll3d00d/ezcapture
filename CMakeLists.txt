cmake_minimum_required(VERSION 3.24)

# Dependencies (quill, prometheus-cpp, gtest, winreg) come from vcpkg manifest mode. The presets
# set the toolchain, but IDE profiles that bypass CMakePresets.json (e.g.
# CLion's default "Debug" profile) would otherwise fail at find_package(quill).
# Must be set before project(). Prefer C:/vcpkg to match CMakePresets.json;
# VCPKG_ROOT is only a fallback because vcvars points it at the VS-bundled vcpkg.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    foreach(_ez_vcpkg_root IN ITEMS "C:/vcpkg" "$ENV{VCPKG_ROOT}")
        if(_ez_vcpkg_root AND EXISTS "${_ez_vcpkg_root}/scripts/buildsystems/vcpkg.cmake")
            set(CMAKE_TOOLCHAIN_FILE "${_ez_vcpkg_root}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "vcpkg toolchain")
            break()
        endif()
    endforeach()
endif()
# static libs + dynamic CRT (/MD) so dependencies (prometheus-cpp, civetweb) are linked into the
# .ax files rather than shipped as DLLs; must match VcpkgTriplet in the vcxproj files.
if(NOT DEFINED VCPKG_TARGET_TRIPLET)
    set(VCPKG_TARGET_TRIPLET x64-windows-static-md CACHE STRING "")
endif()

project(ezcapture LANGUAGES CXX C)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "ezcapture is Windows/MSVC-only (DirectShow, MIDL, Windows SDK, DeckLink/Magewell SDKs).")
endif()

enable_language(RC)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Every vcxproj sets CharacterSet=Unicode, which MSBuild silently expands to
# these two defines. Without them, dead ANSI code paths in the (very old)
# DirectShow base classes compile instead of being #ifdef'd out.
add_compile_definitions(UNICODE _UNICODE)

# Match the old MSBuild x64\<Config>\ output layout.
foreach(_cfg IN ITEMS "" "_DEBUG" "_RELEASE")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY${_cfg} ${CMAKE_BINARY_DIR}/x64/$<CONFIG>)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY${_cfg} ${CMAKE_BINARY_DIR}/x64/$<CONFIG>)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY${_cfg} ${CMAKE_BINARY_DIR}/x64/$<CONFIG>)
endforeach()

# ---------------------------------------------------------------------------
# The original MSBuild solution had 5 named configs: Debug / Release /
# ReleaseNoArch / ReleaseTrace / ReleaseTraceNoArch. Auditing every vcxproj
# showed Release vs *NoArch* differ ONLY in the AVX2 codegen flag, and
# Release vs *Trace* differ ONLY in NO_LOGGING vs CAPTURE_NAME_SUFFIX=1.
# (NO_LOGGING was NO_QUILL; quill is now always linked for the Prometheus metrics.) So
# those two axes become independent options laid over normal Debug/Release
# build types instead of 5 separate named configurations.
# ---------------------------------------------------------------------------
option(EZ_ENABLE_AVX2 "Compile with /arch:AVX2. Turn OFF to reproduce the old *NoArch* configs." ON)
option(EZ_ENABLE_TRACE "Keep Quill logging enabled and tag filters for a -trace build (old *Trace* configs)." OFF)
option(EZ_BUILD_BENCHTEST "Build the benchtest micro-benchmark executable." ON)
option(EZ_BUILD_TESTS "Build mwcapture-test (requires gtest via vcpkg)." ON)

# Only common/common.vcxproj ever defined these; CI supplies them via
# /p:vmajor etc, parsed from the pushed git tag. Empty = "0" (see version.h).
set(EZ_VERSION_MAJOR "" CACHE STRING "Release version major, set by CI from the git tag")
set(EZ_VERSION_MINOR "" CACHE STRING "Release version minor")
set(EZ_VERSION_PATCH "" CACHE STRING "Release version patch")
set(EZ_VERSION_SUFFIX "" CACHE STRING "Release version prerelease suffix, e.g. beta.1")

# Applied to every target except directshow_baseclasses (that one built with
# ConformanceMode=false i.e. no /permissive- in the original project).
function(ez_target_common target)
    target_compile_options(${target} PRIVATE
        /W3 /sdl /permissive-
        $<$<BOOL:${EZ_ENABLE_AVX2}>:/arch:AVX2>
        $<$<CONFIG:Release>:/Gy>
    )
    target_link_options(${target} PRIVATE
        $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>
    )
endfunction()

function(ez_target_common_legacy target)
    # directshow_baseclasses sets no LanguageStandard in the original vcxproj
    # (unlike every other target, pinned to C++20) and ConformanceMode=false;
    # under our global CMAKE_CXX_STANDARD 20 that combination makes MSVC
    # reject this old codebase's non-const string-literal assignments
    # (C2440) unless strict-strings checking is relaxed back off.
    target_compile_options(${target} PRIVATE
        /W3 /sdl /Zc:strictStrings-
        $<$<BOOL:${EZ_ENABLE_AVX2}>:/arch:AVX2>
        $<$<CONFIG:Release>:/Gy>
    )
    target_link_options(${target} PRIVATE
        $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>
    )
endfunction()

add_subdirectory(directshow_baseclasses)
add_subdirectory(DeckLink)
add_subdirectory(common)
add_subdirectory(mwcapture)
add_subdirectory(bmcapture)

if(EZ_BUILD_BENCHTEST)
    add_subdirectory(benchtest)
endif()
if(EZ_BUILD_TESTS)
    add_subdirectory(mwcapture-test)
endif()
