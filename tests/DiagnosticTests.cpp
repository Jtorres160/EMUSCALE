#include "emuscale/core/Diagnostics.h"
#include "emuscale/windows/DisplayInventory.h"
#include "emuscale/windows/WindowDiscovery.h"
#include <cassert>
#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>

int main() {
    using namespace emuscale;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    assert(eden_process_name(L"Eden.exe"));
    assert(eden_process_name(L"eden-nightly.exe"));
    assert(!eden_process_name(L"not-eden.exe"));
    assert(!eden_process_name(L"eden.exe.bak"));
    assert(declared_totk_title(L"Eden | The Legend of Zelda: Tears of the Kingdom"));
    assert(declared_totk_title(L"Eden - TotK"));
    assert(!declared_totk_title(L"Eden library"));
    assert(json_string("a\n\"\\") == "\"a\\u000a\\\"\\\\\"");
    assert(!capturable_window(nullptr));
    const auto displays = discover_displays(false);
    for (const auto& d : displays) { assert(d.width > 0); assert(d.height > 0); assert(d.dpi_x >= 96); }
    constexpr std::int64_t f = 10'000'000;
    FrameMetrics timing;
    timing.on_callback(f, f); timing.on_frame(0, f, f);
    assert(timing.snapshot(f, f).missing_timestamp_total == 1);
    assert(timing.snapshot(f, f).distinct_capture_total == 0);
    timing.on_present(f, f, f - 100'000);
    timing.on_present(f + 600'000, f, f + 500'000);
    auto m = timing.snapshot(f + 600'000, f);
    assert(m.pacing_spikes_total == 1);
    assert(m.max_present_interval_ms == 60);
    assert(m.average_capture_to_present_ms == 10);
    assert(timing.snapshot(f * 20, f).presented_fps == 0);
    assert(timing.snapshot(f * 20, f).average_capture_to_present_ms == -1);
    timing.on_present(f * 21, f);
    assert(timing.snapshot(f * 21, f).pacing_spikes_total == 2);
    assert(timing.snapshot(f * 21, f).max_present_interval_ms > 19000);

    const auto root = std::filesystem::temp_directory_path() / (L"emuscale_diagnostics_test_" + std::to_wstring(GetCurrentProcessId()));
    auto metrics = std::make_shared<FrameMetrics>(); auto stats = std::make_shared<CaptureStats>();
    auto runtime = std::make_shared<DiagnosticRuntime>();
    DiagnosticConfig config; config.target_pid = GetCurrentProcessId();
    config.target_process = L"synthetic-test.exe"; config.scene = "synthetic; not gameplay";
    DiagnosticSession session;
    session.start(root, config, metrics, stats, runtime);
    bool locked = false;
    try { DiagnosticSession another; another.start(root, config, metrics, stats, runtime); }
    catch (const std::exception&) { locked = true; }
    assert(locked);
    for (int i = 0; i < 150; ++i) {
        LARGE_INTEGER n{}, freq{}; QueryPerformanceCounter(&n); QueryPerformanceFrequency(&freq);
        metrics->on_callback(n.QuadPart, freq.QuadPart);
        const auto source = static_cast<std::int64_t>(double(n.QuadPart) * 10'000'000 / freq.QuadPart);
        metrics->on_frame(source, n.QuadPart, freq.QuadPart); metrics->on_present(n.QuadPart, freq.QuadPart, source);
        if (i == 50) session.event("activity", "Stuttering observed");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto folder = session.directory();
    assert(std::filesystem::file_size(folder / "metrics.csv") > 1000); // progressive I/O, before stop
    session.stop("synthetic test finished");
    assert(!session.failed()); assert(!session.active());
    const auto summary = analyze_session(folder);
    assert(summary.find("0 invalid rows") != std::string::npos);
    assert(summary.find("clean session-end event") != std::string::npos);
    std::ifstream event_file(folder / "events.jsonl"); std::stringstream event_text; event_text << event_file.rdbuf(); event_file.close();
    assert(event_text.str().find("Stuttering observed") != std::string::npos);
    { std::ofstream malformed(folder / "metrics.csv", std::ios::app); malformed << "broken,row\n"; }
    assert(analyze_session(folder).find("1 invalid rows") != std::string::npos);
    std::filesystem::remove(folder / "events.jsonl");
    assert(analyze_session(folder).find("partial session") != std::string::npos);
    bool quota = false;
    try { DiagnosticSession too_small; auto small = config; small.storage_limit_bytes = 1; too_small.start(root, small, metrics, stats, runtime); }
    catch (const std::exception&) { quota = true; }
    assert(quota);
    DiagnosticSession bounded;
    auto capped_config = config; capped_config.session_limit_bytes = 64 * 1024;
    bounded.start(root, capped_config, metrics, stats, runtime);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (bounded.active() && std::chrono::steady_clock::now() < deadline) {
        for (int i = 0; i < 1000; ++i) bounded.event("stress-test", std::string(4096, 'x'));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    bounded.stop("bounded writer test");
    assert(!bounded.failed());
    assert(analyze_session(bounded.directory()).find("storage quota") != std::string::npos);
    std::uintmax_t size = 0;
    for (const auto& e : std::filesystem::directory_iterator(bounded.directory())) if (e.is_regular_file()) size += e.file_size();
    assert(size <= capped_config.session_limit_bytes);
    // These are exact test-created paths below the OS temporary directory.
    std::filesystem::remove_all(root);
}
