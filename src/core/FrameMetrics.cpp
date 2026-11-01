#include "emuscale/core/FrameMetrics.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <stdexcept>

namespace emuscale {
namespace {
void trim(std::deque<std::int64_t>& samples, std::int64_t cutoff) {
    while (!samples.empty() && samples.front() < cutoff) samples.pop_front();
    while (samples.size() > 16384) samples.pop_front();
}
void trim_intervals(std::deque<std::pair<std::int64_t, double>>& samples,
                    std::int64_t cutoff) {
    while (!samples.empty() && samples.front().first < cutoff) samples.pop_front();
    while (samples.size() > 16384) samples.pop_front();
}
double average_intervals(const std::deque<std::pair<std::int64_t, double>>& values,
                         std::int64_t cutoff) {
    double total = 0;
    std::size_t count = 0;
    for (const auto& entry : values) {
        if (entry.first < cutoff) continue;
        total += entry.second;
        ++count;
    }
    return count ? total / count : 0;
}
}
void FrameMetrics::on_callback(std::int64_t now, std::int64_t frequency) {
    std::lock_guard lock(mutex_);
    ++callback_total_; // Invocation accounting survives an unusable timing sample.
    if (frequency <= 0 || now <= 0 || (last_callback_qpc_ && now < last_callback_qpc_)) { ++order_errors_; return; }
    if (frequency > 0 && last_callback_qpc_ > 0 && now > last_callback_qpc_)
        callback_intervals_.emplace_back(now,
            1000.0 * (now - last_callback_qpc_) / frequency);
    last_callback_qpc_ = now;
    if (frequency > 0) trim_intervals(callback_intervals_, now - frequency * 10);
    callbacks_.push_back(now);
    trim(callbacks_, now - frequency);
}
void FrameMetrics::on_frame(std::int64_t ticks, std::int64_t now,
                            std::int64_t frequency) {
    std::lock_guard lock(mutex_);
    if (frequency <= 0 || now <= 0 || (last_frame_qpc_ && now < last_frame_qpc_)) { ++order_errors_; return; }
    const double interval = last_frame_qpc_ ? 1000.0 * (now - last_frame_qpc_) / frequency : 0;
    if (last_frame_qpc_) frame_arrival_intervals_.emplace_back(now, interval);
    trim_intervals(frame_arrival_intervals_, now - frequency * 10);
    last_frame_qpc_ = now;
    record_timing({now, ticks, interval, false});
    if (ticks <= 0) { ++missing_; return; }
    if (ticks < last_compositor_ticks_) { ++order_errors_; return; }
    if (ticks == last_compositor_ticks_) ++repeated_total_;
    else {
        // TimeSpan ticks are 100 ns; these intervals describe compositor
        // timestamps, not proof of distinct game image content.
        if (ticks > 0 && last_compositor_ticks_ > 0 && ticks > last_compositor_ticks_)
            source_intervals_.emplace_back(now,
                static_cast<double>(ticks - last_compositor_ticks_) / 10'000.0);
        ++distinct_total_;
        distinct_.push_back(now);
        trim(distinct_, now - frequency);
    }
    if (frequency > 0) trim_intervals(source_intervals_, now - frequency * 10);
    last_compositor_ticks_ = ticks;
}
void FrameMetrics::on_present(std::int64_t now, std::int64_t frequency, std::int64_t source_ticks) {
    on_present_result(now, frequency, source_ticks, true);
}
void FrameMetrics::on_present_result(std::int64_t now, std::int64_t frequency, std::int64_t source_ticks,
    bool accepted, bool occluded, bool failed) {
    std::lock_guard lock(mutex_);
    ++present_calls_;
    if (occluded) ++occluded_;
    if (failed) ++failed_;
    if (!accepted) return;
    ++presented_total_;
    if (source_ticks > 0 && source_ticks <= last_present_source_) ++repeated_present_;
    if (source_ticks <= 0) ++unknown_source_present_;
    last_present_source_ = std::max(last_present_source_, source_ticks);
    if (frequency <= 0 || now <= 0 || (last_present_qpc_ && now < last_present_qpc_)) { ++order_errors_; return; }
    const double ms = last_present_qpc_ ? 1000.0 * (now - last_present_qpc_) / frequency : 0;
    if (last_present_qpc_ && now > last_present_qpc_) {
        if (ms > spike_ms_) ++spikes_;
        if (ms > warning_ms_) ++warnings_;
        present_intervals_.emplace_back(now, ms);
        trim_intervals(present_intervals_, now - frequency * 10);
    }
    last_present_qpc_ = now;
    record_timing({now, source_ticks, ms, true});
    if (frequency > 0 && source_ticks > 0) {
        const double delay = 1000.0 * double(now) / frequency - double(source_ticks) / 10'000.0;
        if (delay >= 0 && delay < 60'000) delays_.emplace_back(now, delay);
        trim_intervals(delays_, now - frequency * 10);
    }
    presentations_.push_back(now);
    trim(presentations_, now - frequency * 10);
}
void FrameMetrics::on_skipped(bool minimized) {
    std::lock_guard lock(mutex_);
    if (minimized) ++minimized_skipped_; else ++duplicate_skipped_;
}
void FrameMetrics::set_thresholds(double warning, double spike) {
    if (!std::isfinite(warning) || !std::isfinite(spike) || warning <= 0 || spike < warning) throw std::invalid_argument("Require 0 < warning threshold <= spike threshold");
    std::lock_guard lock(mutex_); warning_ms_ = warning; spike_ms_ = spike;
}
void FrameMetrics::record_timing(TimingRecord record) {
    if (timings_.size() >= 8192) { timings_.pop_front(); ++records_dropped_; }
    timings_.push_back(record);
}
std::vector<TimingRecord> FrameMetrics::drain_timings() {
    std::lock_guard lock(mutex_);
    std::vector<TimingRecord> out(timings_.begin(), timings_.end()); timings_.clear(); return out;
}
void FrameMetrics::on_capped() noexcept {
    std::lock_guard lock(mutex_);
    ++capped_total_;
}
MetricsSnapshot FrameMetrics::snapshot(std::int64_t now, std::int64_t frequency) const {
    std::lock_guard lock(mutex_);
    MetricsSnapshot result;
    result.callback_total = callback_total_;
    result.distinct_capture_total = distinct_total_;
    result.presented_total = presented_total_;
    result.repeated_timestamp_total = repeated_total_;
    result.capped_total = capped_total_;
    result.pacing_spikes_total = spikes_;
    result.missing_timestamp_total = missing_;
    result.present_calls_total = present_calls_; result.occluded_total = occluded_; result.present_failed_total = failed_;
    result.repeated_present_total = repeated_present_; result.duplicate_skipped_total = duplicate_skipped_; result.minimized_skipped_total = minimized_skipped_;
    result.timestamp_order_errors = order_errors_; result.timing_records_dropped_total = records_dropped_; result.warning_intervals_total = warnings_;
    result.last_capture_qpc = last_frame_qpc_; result.last_source_timestamp_100ns = last_compositor_ticks_;
    result.last_callback_qpc = last_callback_qpc_;
    result.unknown_source_present_total = unknown_source_present_;
    result.new_source_present_total = presented_total_ - repeated_present_ - unknown_source_present_;
    if (frequency <= 0) return result;
    const auto cutoff = now - frequency;
    auto count_recent = [cutoff](const auto& values) {
        return static_cast<double>(std::count_if(values.begin(), values.end(),
            [cutoff](auto value) { return value >= cutoff; }));
    };
    result.callback_fps = count_recent(callbacks_);
    result.distinct_capture_fps = count_recent(distinct_);
    result.presented_fps = count_recent(presentations_);
    result.average_source_ms = average_intervals(source_intervals_, now - frequency * 10);
    result.average_callback_ms = average_intervals(callback_intervals_, now - frequency * 10);
    std::vector<double> capture_intervals;
    for (const auto& value : frame_arrival_intervals_) if (value.first >= now - frequency * 10) capture_intervals.push_back(value.second);
    result.capture_intervals = interval_statistics(std::move(capture_intervals));
    if (std::any_of(delays_.begin(), delays_.end(), [&](const auto& d) { return d.first >= now - frequency * 10; }))
        result.average_capture_to_present_ms = average_intervals(delays_, now - frequency * 10);
    std::vector<double> intervals;
    for (const auto& interval : present_intervals_)
        if (interval.first >= now - frequency * 10) intervals.push_back(interval.second);
    if (!intervals.empty()) {
        result.presentation_intervals = interval_statistics(intervals);
        double total = 0;
        for (double ms : intervals) total += ms;
        result.average_present_ms = total / intervals.size();
        result.max_present_interval_ms = *std::max_element(intervals.begin(), intervals.end());
        double squared = 0;
        for (double ms : intervals) squared += (ms - result.average_present_ms) *
                                               (ms - result.average_present_ms);
        result.pacing_stddev_ms = std::sqrt(squared / intervals.size());
        if (intervals.size() >= 100) {
            std::sort(intervals.begin(), intervals.end());
            const auto index = static_cast<std::size_t>(0.99 * (intervals.size() - 1));
            if (intervals[index] > 0) result.one_percent_low_fps = 1000.0 / intervals[index];
        }
    }
    return result;
}
} // namespace emuscale
