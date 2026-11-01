cmake_minimum_required(VERSION 3.21)
project(EmuScaleRTX VERSION 0.1.1 LANGUAGES CXX)

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
    src/core/Benchmark.cpp)
target_compile_features(emuscale_capabilities PUBLIC cxx_std_20)
target_include_directories(emuscale_capabilities PUBLIC include)
target_link_libraries(emuscale_capabilities PRIVATE dxgi d3d11)
target_compile_definitions(emuscale_capabilities PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

add_executable(emuscale_rtx
    src/app/EmuScaleMain.cpp
    src/windows/WindowDiscovery.cpp
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

option(EMUSCALE_BUILD_TESTS "Build capability policy tests" ON)
if(EMUSCALE_BUILD_TESTS)
    enable_testing()
    add_executable(capability_tests tests/CapabilityTests.cpp)
    target_link_libraries(capability_tests PRIVATE emuscale_capabilities)
    add_test(NAME capability_tests COMMAND capability_tests)
    add_executable(pacing_tests tests/PacingTests.cpp)
    target_link_libraries(pacing_tests PRIVATE emuscale_capabilities)
    add_test(NAME pacing_tests COMMAND pacing_tests)
endif()
