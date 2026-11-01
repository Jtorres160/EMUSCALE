#include "emuscale/core/FrameMetrics.h"
#include <cassert>
#include <cmath>
#include <limits>
int main() {
    using namespace emuscale;
    auto stats = interval_statistics({10, 20, 30, 40, 100});
    assert(stats.count == 5 && stats.mean_ms == 40 && stats.min_ms == 10 && stats.max_ms == 100);
    assert(stats.p50_ms == 30 && std::abs(stats.p95_ms - 88) < 1e-8 && std::abs(stats.p99_ms - 97.6) < 1e-8);
    assert(interval_statistics({}).count == 0);
    bool rejected = false; try { interval_statistics({std::numeric_limits<double>::quiet_NaN()}); } catch (...) { rejected = true; } assert(rejected);
    std::vector<ActivityMarker> markers{{10, "Combat"}, {11, "Combat"}, {20, "Menu"}};
    auto correlations = correlate_markers(markers, {{10, 60, "capture"}, {12, 70, "present"}, {50, 100, "present"}});
    assert(correlations[0].label == "Combat" && correlations[0].markers == 2 && correlations[0].nearby_anomalies == 2);
    assert(correlations[1].nearby_anomalies == 0);
    assert(capture_condition(.1, .1, false, false, true, true) == CaptureCondition::waiting);
    assert(capture_condition(1, .8, true, false, true, true) == CaptureCondition::stalled);
    assert(capture_condition(2, .01, true, false, true, true) == CaptureCondition::active); // recovery
    assert(capture_condition(3, 1, true, true, true, true) == CaptureCondition::minimized);
    assert(capture_condition(4, 1, true, false, false, true) == CaptureCondition::replaced);
    assert(capture_condition(4, 1, true, false, false, false) == CaptureCondition::closed);
    assert(capture_retry_delay(0, false) == 1 && capture_retry_delay(1, false) == 2 && capture_retry_delay(2, false) == 4);
    assert(capture_retry_delay(3, false) == 0 && capture_retry_delay(0, true) == 0);
    FrameMetrics m; m.set_thresholds(33.3, 50);
    constexpr std::int64_t f = 1'000'000;
    m.on_callback(f, f); m.on_frame(10'000'000, f, f); m.on_present(f, f, 10'000'000);
    m.on_callback(f + 40'000, f); m.on_frame(10'400'000, f + 40'000, f);
    m.on_present(f + 40'000, f, 10'000'000); // deliberately repeated presentation
    m.on_present_result(f + 50'000, f, 10'400'000, false, true);
    m.on_present_result(f + 60'000, f, 10'400'000, false, false, true);
    m.on_present(f + 100'000, f, 10'400'000);
    m.on_skipped(true); m.on_skipped(false); m.on_capped();
    m.on_callback(f - 1, f); // invalid monotonic ordering cannot corrupt FPS
    auto report = m.snapshot(f + 110'000, f);
    assert(report.present_calls_total == 5 && report.presented_total == 3 && report.repeated_present_total == 1);
    assert(report.distinct_capture_total == 2 && report.generated_total == 0);
    assert(report.occluded_total == 1 && report.present_failed_total == 1 && report.timestamp_order_errors == 1);
    assert(report.callback_total == 3); // Bad timing cannot erase an actual callback.
    assert(report.warning_intervals_total == 2 && report.pacing_spikes_total == 1);
    assert(report.presentation_intervals.p50_ms == 50 && report.capture_intervals.mean_ms == 40);
    assert(report.minimized_skipped_total == 1 && report.duplicate_skipped_total == 1);
    auto raw = m.drain_timings(); assert(raw.size() == 5 && m.drain_timings().empty());
    for (int i = 0; i < 9000; ++i) m.on_present(f * 2 + i, f);
    assert(m.drain_timings().size() == 8192);
    assert(m.snapshot(f * 3, f).timing_records_dropped_total == 808);
    FrameMetrics ordering;
    ordering.on_callback(f, f); ordering.on_frame(100, f, f);
    ordering.on_callback(f + 100, f); ordering.on_frame(90, f + 100, f);
    assert(ordering.snapshot(f + 100, f).distinct_capture_total == 1);
    assert(ordering.snapshot(f + 100, f).timestamp_order_errors == 1);
    FrameMetrics invalid_present;
    invalid_present.on_present(0, f, 5);
    const auto invalid_clock = invalid_present.snapshot(f, f);
    assert(invalid_clock.present_calls_total == 1 && invalid_clock.presented_total == 1);
    assert(invalid_clock.new_source_present_total == 1 && invalid_clock.timestamp_order_errors == 1 && invalid_clock.presented_fps == 0);
}
