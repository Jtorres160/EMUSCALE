#pragma once
#include <cstdint>
#include <deque>
#include <mutex>
#include <utility>

namespace emuscale {
struct MetricsSnapshot {
    double distinct_capture_fps = 0;
    double callback_fps = 0;
    double presented_fps = 0;
    double average_source_ms = 0;
    double average_callback_ms = 0;
    double average_present_ms = 0;
    double one_percent_low_fps = 0;
    double pacing_stddev_ms = 0;
    double max_present_interval_ms = 0;
    double average_capture_to_present_ms = -1; // no valid clock-correlated samples
    std::uint64_t pacing_spikes_total = 0;
    std::uint64_t missing_timestamp_total = 0;
    std::uint64_t distinct_capture_total = 0;
    std::uint64_t callback_total = 0;
    std::uint64_t presented_total = 0;
    std::uint64_t repeated_timestamp_total = 0;
    std::uint64_t capped_total = 0;
    std::uint64_t generated_total = 0;
};

// Capture timestamps are WGC compositor timestamps, not proof of unique game
// images. No GPU readback is used in the normal presentation path.
class FrameMetrics {
public:
    void on_callback(std::int64_t now_qpc, std::int64_t frequency);
    void on_frame(std::int64_t compositor_ticks, std::int64_t now_qpc,
                  std::int64_t frequency);
    void on_present(std::int64_t now_qpc, std::int64_t frequency, std::int64_t source_ticks = 0);
    void on_capped() noexcept;
    [[nodiscard]] MetricsSnapshot snapshot(std::int64_t now_qpc,
                                           std::int64_t frequency) const;
private:
    mutable std::mutex mutex_;
    std::deque<std::int64_t> callbacks_;
    std::deque<std::int64_t> distinct_;
    std::deque<std::int64_t> presentations_;
    std::deque<std::pair<std::int64_t, double>> source_intervals_;
    std::deque<std::pair<std::int64_t, double>> callback_intervals_;
    std::deque<std::pair<std::int64_t, double>> delays_;
    std::deque<std::pair<std::int64_t, double>> present_intervals_;
    std::uint64_t spikes_ = 0, missing_ = 0;
    std::int64_t last_compositor_ticks_ = 0;
    std::int64_t last_callback_qpc_ = 0;
    std::uint64_t callback_total_ = 0, distinct_total_ = 0;
    std::uint64_t presented_total_ = 0, repeated_total_ = 0;
    std::uint64_t capped_total_ = 0;
};
} // namespace emuscale
