#include "emuscale/core/TimingAnalysis.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <stdexcept>

namespace emuscale {
IntervalStats interval_statistics(std::vector<double> intervals) {
    IntervalStats out;
    for (double ms : intervals) if (!std::isfinite(ms) || ms < 0) throw std::invalid_argument("Invalid frame interval");
    if (intervals.empty()) return out;
    std::sort(intervals.begin(), intervals.end());
    out.count = intervals.size(); out.min_ms = intervals.front(); out.max_ms = intervals.back();
    out.mean_ms = std::accumulate(intervals.begin(), intervals.end(), 0.0) / intervals.size();
    auto percentile = [&](double p) {
        const double position = p * (intervals.size() - 1);
        const auto lo = static_cast<std::size_t>(position);
        return intervals[lo] + (intervals[std::min(lo + 1, intervals.size() - 1)] - intervals[lo]) * (position - lo);
    };
    out.p50_ms = percentile(.5); out.p95_ms = percentile(.95); out.p99_ms = percentile(.99);
    return out;
}
std::vector<MarkerCorrelation> correlate_markers(const std::vector<ActivityMarker>& markers,
    const std::vector<FrameAnomaly>& anomalies, double radius_s) {
    if (!std::isfinite(radius_s) || radius_s < 0) throw std::invalid_argument("Invalid correlation radius");
    std::map<std::string, std::vector<double>> times;
    for (const auto& m : markers) if (std::isfinite(m.elapsed_s) && m.elapsed_s >= 0) times[m.label].push_back(m.elapsed_s);
    std::vector<MarkerCorrelation> out;
    for (auto& [label, values] : times) {
        std::sort(values.begin(), values.end());
        MarkerCorrelation result{label, values.size(), 0};
        // Count an anomaly once per label, even when two same-label windows overlap.
        for (const auto& a : anomalies) {
            auto next = std::lower_bound(values.begin(), values.end(), a.elapsed_s - radius_s);
            if (next != values.end() && *next <= a.elapsed_s + radius_s) {
                ++result.nearby_anomalies;
                if (a.stream == "capture") ++result.nearby_capture;
                if (a.stream == "present") ++result.nearby_present;
            }
        }
        out.push_back(std::move(result));
    }
    return out;
}
CaptureCondition capture_condition(double elapsed, double gap, bool frame, bool minimized,
    bool same_window, bool exists, double stall_s) {
    if (!exists) return CaptureCondition::closed;
    if (!same_window) return CaptureCondition::replaced;
    if (minimized) return CaptureCondition::minimized;
    if (!frame) return elapsed < stall_s ? CaptureCondition::waiting : CaptureCondition::stalled;
    return gap >= stall_s ? CaptureCondition::stalled : CaptureCondition::active;
}
const char* condition_name(CaptureCondition c) {
    switch (c) {
    case CaptureCondition::waiting: return "waiting";
    case CaptureCondition::active: return "active";
    case CaptureCondition::stalled: return "no acquired frames";
    case CaptureCondition::minimized: return "target minimized";
    case CaptureCondition::closed: return "target closed";
    default: return "target identity replaced";
    }
}
unsigned capture_retry_delay(unsigned attempts, bool device_lost) {
    return !device_lost && attempts < 3 ? 1u << attempts : 0;
}
}
