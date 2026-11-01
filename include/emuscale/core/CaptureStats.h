#pragma once
#include <atomic>
#include <cstdint>

namespace emuscale {
struct CaptureStats {
    std::atomic<std::uint64_t> captured{0};
    std::atomic<std::uint64_t> presented{0};
    std::atomic<std::uint64_t> overwritten{0}; // frames replaced before presentation
    std::atomic<std::uint32_t> capture_width{0};
    std::atomic<std::uint32_t> capture_height{0};
    std::atomic<bool> target_closed{false};
    std::atomic<bool> capture_failed{false};
    std::atomic<long> capture_hresult{0};
};
} // namespace emuscale
