#include "emuscale/core/FrameMetrics.h"
#include <algorithm>
#include <cmath>
#include <vector>

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
    if (frequency > 0 && last_callback_qpc_ > 0 && now > last_callback_qpc_)
        callback_intervals_.emplace_back(now,
            1000.0 * (now - last_callback_qpc_) / frequency);
    last_callback_qpc_ = now;
    if (frequency > 0) trim_intervals(callback_intervals_, now - frequency * 10);
    ++callback_total_;
    callbacks_.push_back(now);
    trim(callbacks_, now - frequency);
}
void FrameMetrics::on_frame(std::int64_t ticks, std::int64_t now,
                            std::int64_t frequency) {
    std::lock_guard lock(mutex_);
    if (ticks <= 0) { ++missing_; return; }
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
    std::lock_guard lock(mutex_);
    // Fixed >50 ms threshold; cumulative count does not miss spikes between samples.
    if (frequency > 0 && !presentations_.empty() && now > presentations_.back()) {
        const double ms = 1000.0 * (now - presentations_.back()) / frequency;
        if (ms > 50.0) ++spikes_;
        present_intervals_.emplace_back(now, ms);
        trim_intervals(present_intervals_, now - frequency * 10);
    }
    if (frequency > 0 && source_ticks > 0) {
        const double delay = 1000.0 * double(now) / frequency - double(source_ticks) / 10'000.0;
        if (delay >= 0 && delay < 60'000) delays_.emplace_back(now, delay);
        trim_intervals(delays_, now - frequency * 10);
    }
    ++presented_total_;
    presentations_.push_back(now);
    trim(presentations_, now - frequency * 10);
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
    if (std::any_of(delays_.begin(), delays_.end(), [&](const auto& d) { return d.first >= now - frequency * 10; }))
        result.average_capture_to_present_ms = average_intervals(delays_, now - frequency * 10);
    std::vector<double> intervals;
    for (const auto& interval : present_intervals_)
        if (interval.first >= now - frequency * 10) intervals.push_back(interval.second);
    if (!intervals.empty()) {
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
