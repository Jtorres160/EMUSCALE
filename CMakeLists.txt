cmake_minimum_required(VERSION 3.21)
project(EmuScaleRTX VERSION 0.1.3 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "EmuScale RTX currently requires Windows")
endif()
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "EmuScale RTX v0.1 requires a 64-bit build")
endif()

add_library(emuscale_capabilities STATIC
    src/core/GpuCapabilities.cpp
    src/core/WindowsGpuProbe.cpp
    src/core/FramePacing.cpp
    src/core/FrameMetrics.cpp
    src/core/Benchmark.cpp
    src/core/Diagnostics.cpp
    src/core/TimingAnalysis.cpp
    src/graphics/ProcessingPipeline.cpp
    src/windows/DisplayPolicy.cpp
    src/windows/DisplayInventory.cpp
    src/windows/WindowDiscovery.cpp)
target_compile_features(emuscale_capabilities PUBLIC cxx_std_20)
target_include_directories(emuscale_capabilities PUBLIC include)
target_link_libraries(emuscale_capabilities PRIVATE dxgi d3d11 user32 shcore psapi pdh)
target_compile_definitions(emuscale_capabilities PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
    _WIN32_WINNT=0x0A00 WINVER=0x0A00)

add_executable(emuscale_rtx
    src/app/EmuScaleMain.cpp
    src/capture/WindowCapture.cpp
    src/presentation/PreviewRenderer.cpp)
target_compile_features(emuscale_rtx PRIVATE cxx_std_20)
target_include_directories(emuscale_rtx PRIVATE include)
target_compile_definitions(emuscale_rtx PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
    _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(emuscale_rtx PRIVATE emuscale_capabilities d3d11 dxgi
    d3dcompiler windowsapp user32)

add_executable(emuscale_gpu_info src/app/GpuInfoMain.cpp)
target_link_libraries(emuscale_gpu_info PRIVATE emuscale_capabilities)
add_executable(emuscale_analyze src/app/AnalyzeMain.cpp)
target_link_libraries(emuscale_analyze PRIVATE emuscale_capabilities)

option(EMUSCALE_BUILD_TESTS "Build capability policy tests" ON)
if(EMUSCALE_BUILD_TESTS)
    enable_testing()
    add_executable(capability_tests tests/CapabilityTests.cpp)
    target_link_libraries(capability_tests PRIVATE emuscale_capabilities)
    add_test(NAME capability_tests COMMAND capability_tests)
    add_executable(pacing_tests tests/PacingTests.cpp)
    target_link_libraries(pacing_tests PRIVATE emuscale_capabilities)
    add_test(NAME pacing_tests COMMAND pacing_tests)
    add_executable(diagnostic_tests tests/DiagnosticTests.cpp)
    target_link_libraries(diagnostic_tests PRIVATE emuscale_capabilities)
    add_test(NAME diagnostic_tests COMMAND diagnostic_tests)
    add_executable(timing_tests tests/TimingTests.cpp)
    target_link_libraries(timing_tests PRIVATE emuscale_capabilities)
    add_test(NAME timing_tests COMMAND timing_tests)
    add_executable(display_policy_tests tests/DisplayPolicyTests.cpp)
    target_link_libraries(display_policy_tests PRIVATE emuscale_capabilities)
    add_test(NAME display_policy_tests COMMAND display_policy_tests)
    add_executable(processing_tests tests/ProcessingTests.cpp)
    target_link_libraries(processing_tests PRIVATE emuscale_capabilities)
    add_test(NAME processing_tests COMMAND processing_tests)
    add_test(NAME diagnostics_help COMMAND emuscale_rtx --help)
    add_test(NAME diagnostics_B_requires_ready COMMAND emuscale_rtx --diagnostics --scenario=B)
    add_test(NAME diagnostics_C_requires_ready COMMAND emuscale_rtx --diagnostics --scenario=C)
    add_test(NAME diagnostics_B_rejects_disabled_optimizer COMMAND emuscale_rtx --diagnostics --scenario=B --ready --optimizer=disabled --scene=test)
    add_test(NAME diagnostics_A_requires_unlocked COMMAND emuscale_rtx --diagnostics --limit=60)
    add_test(NAME diagnostics_rejects_malformed_duration COMMAND emuscale_rtx --diagnostics --duration=60oops)
    add_test(NAME analyzer_rejects_missing_session COMMAND emuscale_analyze __missing_session__)
    add_test(NAME diagnostics_rejects_bad_thresholds COMMAND emuscale_rtx --diagnostics --warning-ms=60 --spike-ms=50)
    add_test(NAME diagnostics_rejects_nonfinite_threshold COMMAND emuscale_rtx --diagnostics --spike-ms=nan)
    set_tests_properties(diagnostics_B_requires_ready diagnostics_C_requires_ready
        diagnostics_B_rejects_disabled_optimizer diagnostics_A_requires_unlocked
        diagnostics_rejects_malformed_duration analyzer_rejects_missing_session diagnostics_rejects_bad_thresholds
        diagnostics_rejects_nonfinite_threshold PROPERTIES WILL_FAIL TRUE)
    # Tests use assert, including Release configurations.
    foreach(test_target capability_tests pacing_tests diagnostic_tests timing_tests display_policy_tests processing_tests)
        if(MSVC)
            target_compile_options(${test_target} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${test_target} PRIVATE -UNDEBUG)
        endif()
    endforeach()
endif()
