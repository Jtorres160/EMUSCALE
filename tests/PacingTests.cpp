#include "emuscale/core/Benchmark.h"
#include "emuscale/core/FrameMetrics.h"
#include "emuscale/core/FramePacing.h"
#include <cassert>
#include <filesystem>
#include <fstream>

int main() {
    using namespace emuscale;
    assert(fps_cap(FpsLimit::automatic) == 0);
    assert(fps_cap(FpsLimit::fps30) == 30);
    assert(fps_cap(FpsLimit::fps240) == 240);
    assert(fps_cap(FpsLimit::unlocked) == 0);
    assert(present_policy(VSyncMode::automatic, true).sync_interval == 1);
    assert(!present_policy(VSyncMode::on, true).allow_tearing);
    assert(present_policy(VSyncMode::off, true).allow_tearing);
    assert(!present_policy(VSyncMode::off, false).allow_tearing);
    FramePacer pacer;
    constexpr std::int64_t frequency = 1'000'000;
    assert(pacer.allow(1'000'000, frequency)); // Auto: no software cap.
    pacer.set_limit(FpsLimit::fps30);
    assert(pacer.allow(1'000'000, frequency));
    assert(!pacer.allow(1'010'000, frequency));
    assert(pacer.allow(1'034'000, frequency));
    pacer.set_limit(FpsLimit::fps120);
    assert(pacer.allow(1'035'000, frequency));
    pacer.set_limit(FpsLimit::unlocked);
    assert(pacer.allow(1'035'001, frequency));

    FrameMetrics metrics;
    metrics.on_callback(1'000'000, frequency);
    metrics.on_frame(10'000'000, 1'000'000, frequency);
    metrics.on_callback(1'010'000, frequency);
    metrics.on_frame(10'000'000, 1'010'000, frequency); // same compositor timestamp
    metrics.on_callback(1'020'000, frequency);
    metrics.on_frame(10'200'000, 1'020'000, frequency);
    metrics.on_present(1'000'000, frequency);
    metrics.on_present(1'020'000, frequency);
    metrics.on_capped();
    const auto report = metrics.snapshot(1'030'000, frequency);
    assert(report.callback_fps == 3);
    assert(report.distinct_capture_fps == 2);
    assert(report.presented_fps == 2);
    assert(report.repeated_timestamp_total == 1);
    assert(report.capped_total == 1);
    assert(report.generated_total == 0);
    assert(report.average_present_ms == 20);
    assert(report.average_source_ms == 20);
    assert(report.average_callback_ms == 10);

    Benchmark benchmark;
    benchmark.start(1'000'000, 30);
    assert(!benchmark.expired(30'000'000, frequency));
    assert(benchmark.expired(31'000'000, frequency));
    benchmark.add({1.0, report, 60, 0, 1920, 1080, 1280, 720, VSyncMode::off});
    const auto folder = std::filesystem::temp_directory_path() / L"emuscale_pacing_test";
    const auto csv = benchmark.stop_and_export(folder, L"Test GPU");
    assert(std::filesystem::exists(csv));
    std::ifstream file(csv);
    std::string header, row;
    std::getline(file, header);
    std::getline(file, row);
    assert(header.find("distinct_capture_fps") != std::string::npos);
    assert(row.find("Test GPU") != std::string::npos);
    file.close();
    std::filesystem::remove(csv);
    std::filesystem::remove(folder);
}
