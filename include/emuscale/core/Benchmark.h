#pragma once
#include "emuscale/core/FrameMetrics.h"
#include "emuscale/core/FramePacing.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace emuscale {
struct BenchmarkRow {
    double elapsed_seconds = 0;
    MetricsSnapshot metrics;
    int target_fps = 0;
    std::uint64_t overwritten = 0;
    unsigned capture_width = 0, capture_height = 0;
    unsigned output_width = 0, output_height = 0;
    VSyncMode vsync = VSyncMode::automatic;
};

class Benchmark {
public:
    void start(std::int64_t now_qpc, int duration_seconds);
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] int duration_seconds() const noexcept { return duration_seconds_; }
    [[nodiscard]] double elapsed(std::int64_t now_qpc,
                                 std::int64_t frequency) const noexcept;
    void add(BenchmarkRow row);
    [[nodiscard]] bool expired(std::int64_t now_qpc,
                               std::int64_t frequency) const noexcept;
    [[nodiscard]] std::filesystem::path stop_and_export(
        const std::filesystem::path& directory, const std::wstring& gpu_name);
    [[nodiscard]] const std::vector<BenchmarkRow>& rows() const noexcept { return rows_; }
private:
    bool active_ = false;
    int duration_seconds_ = 30;
    std::int64_t start_qpc_ = 0;
    std::vector<BenchmarkRow> rows_;
};
} // namespace emuscale
