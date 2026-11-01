#pragma once
#include <cstdint>
#include <deque>
#include <mutex>
#include <utility>
#include <vector>
#include "emuscale/core/TimingAnalysis.h"

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
    std::uint64_t present_calls_total = 0, occluded_total = 0, present_failed_total = 0;
    std::uint64_t repeated_present_total = 0, duplicate_skipped_total = 0, minimized_skipped_total = 0;
    std::uint64_t timestamp_order_errors = 0, timing_records_dropped_total = 0;
    std::uint64_t warning_intervals_total = 0;
    std::uint64_t new_source_present_total = 0, unknown_source_present_total = 0;
    std::int64_t last_capture_qpc = 0, last_callback_qpc = 0, last_source_timestamp_100ns = 0;
    IntervalStats capture_intervals, presentation_intervals;
};
struct TimingRecord {
    std::int64_t qpc = 0, source_timestamp_100ns = 0;
    double interval_ms = 0;
    bool presentation = false;
};

// Capture timestamps are WGC compositor timestamps, not proof of unique game
// images. No GPU readback is used in the normal presentation path.
class FrameMetrics {
public:
    void on_callback(std::int64_t now_qpc, std::int64_t frequency);
    void on_frame(std::int64_t compositor_ticks, std::int64_t now_qpc,
                  std::int64_t frequency);
    void on_present(std::int64_t now_qpc, std::int64_t frequency, std::int64_t source_ticks = 0);
    void on_present_result(std::int64_t now_qpc, std::int64_t frequency, std::int64_t source_ticks,
                           bool accepted, bool occluded = false, bool failed = false);
    void on_skipped(bool minimized);
    void set_thresholds(double warning_ms, double spike_ms);
    std::vector<TimingRecord> drain_timings();
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
    std::deque<std::pair<std::int64_t, double>> frame_arrival_intervals_;
    std::uint64_t spikes_ = 0, missing_ = 0;
    std::int64_t last_compositor_ticks_ = 0;
    std::int64_t last_callback_qpc_ = 0;
    std::uint64_t callback_total_ = 0, distinct_total_ = 0;
    std::uint64_t presented_total_ = 0, repeated_total_ = 0;
    std::uint64_t capped_total_ = 0;
    void record_timing(TimingRecord record);
    std::deque<TimingRecord> timings_;
    std::int64_t last_frame_qpc_ = 0, last_present_qpc_ = 0, last_present_source_ = 0;
    std::uint64_t present_calls_ = 0, occluded_ = 0, failed_ = 0, repeated_present_ = 0;
    std::uint64_t duplicate_skipped_ = 0, minimized_skipped_ = 0, order_errors_ = 0, records_dropped_ = 0, warnings_ = 0;
    std::uint64_t unknown_source_present_ = 0;
    double warning_ms_ = 33.3, spike_ms_ = 50;
};
} // namespace emuscale
