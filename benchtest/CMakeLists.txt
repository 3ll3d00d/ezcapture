add_executable(benchtest bench.cpp)

target_compile_definitions(benchtest PRIVATE
    _CONSOLE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
)

ez_target_common(benchtest)

# benchmarks the production kernels in common/conversion_kernels.h, unlike
# benchtest which hosts standalone research variants of those kernels
add_executable(kernelbench kernelbench.cpp)

target_compile_definitions(kernelbench PRIVATE
    _CONSOLE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
)

ez_target_common(kernelbench)
