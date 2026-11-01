#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace emuscale {
struct IntervalStats {
    std::uint64_t count = 0;
    double mean_ms = 0, min_ms = 0, max_ms = 0, p50_ms = 0, p95_ms = 0, p99_ms = 0;
};
IntervalStats interval_statistics(std::vector<double> intervals);
struct ActivityMarker { double elapsed_s = 0; std::string label; };
struct FrameAnomaly { double elapsed_s = 0, interval_ms = 0; std::string stream; };
struct MarkerCorrelation { std::string label; std::size_t markers = 0, nearby_anomalies = 0, nearby_capture = 0, nearby_present = 0; };
std::vector<MarkerCorrelation> correlate_markers(const std::vector<ActivityMarker>& markers,
    const std::vector<FrameAnomaly>& anomalies, double radius_s = 2.0);
enum class CaptureCondition { waiting, active, stalled, minimized, closed, replaced };
// No side effects: a stall means no acquired frames, not proof of a game/render fault.
CaptureCondition capture_condition(double elapsed_s, double seconds_since_frame,
    bool has_frame, bool minimized, bool same_window, bool exists, double stall_s = .5);
const char* condition_name(CaptureCondition condition);
unsigned capture_retry_delay(unsigned completed_attempts, bool device_lost);
}
