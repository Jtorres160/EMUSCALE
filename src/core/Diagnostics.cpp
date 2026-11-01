#include "emuscale/core/Diagnostics.h"
#include "emuscale/windows/DisplayInventory.h"
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace emuscale {
namespace {
using Clock = std::chrono::steady_clock;
std::pair<std::int64_t, std::int64_t> qpc() {
    LARGE_INTEGER n{}, f{}; QueryPerformanceCounter(&n); QueryPerformanceFrequency(&f);
    return {n.QuadPart, f.QuadPart};
}
std::string utc() {
    SYSTEMTIME t{}; GetSystemTime(&t);
    std::ostringstream s;
    s << std::setfill('0') << std::setw(4) << t.wYear << '-' << std::setw(2) << t.wMonth << '-'
      << std::setw(2) << t.wDay << 'T' << std::setw(2) << t.wHour << ':' << std::setw(2) << t.wMinute
      << ':' << std::setw(2) << t.wSecond << '.' << std::setw(3) << t.wMilliseconds << 'Z';
    return s.str();
}
std::uint64_t ticks(FILETIME t) { return (std::uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime; }
struct ProcessUsage {
    HANDLE handle = nullptr;
    std::uint64_t last_cpu = 0;
    Clock::time_point last_time{};
    bool valid = false;
    explicit ProcessUsage(DWORD pid) { handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid); }
    ~ProcessUsage() { if (handle) CloseHandle(handle); }
    std::pair<double, double> sample() {
        FILETIME created{}, exited{}, kernel{}, user{};
        PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
        if (!handle || !GetProcessTimes(handle, &created, &exited, &kernel, &user)) { valid = false; return {-1, -1}; }
        const auto cpu = ticks(kernel) + ticks(user);
        const auto now = Clock::now();
        const double elapsed = std::chrono::duration<double>(now - last_time).count();
        const unsigned processors = std::max<DWORD>(1, GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
        double percent = last_time != Clock::time_point{} && elapsed > 0 ?
            100.0 * double(cpu - last_cpu) / 10'000'000 / elapsed / processors : -1;
        last_cpu = cpu; last_time = now; valid = true;
        const double mb = GetProcessMemoryInfo(handle, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)) ?
            double(memory.WorkingSetSize) / (1024 * 1024) : -1;
        return {percent, mb};
    }
};
struct GpuUsage {
    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER counter = nullptr;
    GpuUsage() {
        if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) return;
        if (PdhAddEnglishCounterW(query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &counter) != ERROR_SUCCESS) counter = nullptr;
        if (counter) PdhCollectQueryData(query);
    }
    ~GpuUsage() { if (query) PdhCloseQuery(query); }
    std::pair<double, double> sample(DWORD target) {
        double app = -1, game = -1;
        if (!counter || PdhCollectQueryData(query) != ERROR_SUCCESS) return {app, game};
        DWORD bytes = 0, count = 0;
        if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bytes, &count, nullptr) != PDH_MORE_DATA || bytes > 4 * 1024 * 1024) return {app, game};
        std::vector<unsigned char> buffer(bytes);
        auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
        if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bytes, &count, items) != ERROR_SUCCESS) return {app, game};
        const auto own = L"pid_" + std::to_wstring(GetCurrentProcessId()) + L"_";
        const auto eden = L"pid_" + std::to_wstring(target) + L"_";
        for (DWORD i = 0; i < count; ++i) {
            const auto& v = items[i].FmtValue;
            if (v.CStatus != PDH_CSTATUS_VALID_DATA && v.CStatus != PDH_CSTATUS_NEW_DATA) continue;
            if (!std::isfinite(v.doubleValue)) continue;
            const std::wstring name(items[i].szName);
            if (name.starts_with(own)) app = std::max(app, std::clamp(v.doubleValue, 0.0, 100.0));
            if (name.starts_with(eden)) game = std::max(game, std::clamp(v.doubleValue, 0.0, 100.0));
        }
        return {app, game};
    }
};
const char* csv_header = "elapsed_s,sample_interval_s,callback_fps,distinct_capture_fps,presented_fps,callback_total,distinct_total,presented_total,average_source_ms,average_callback_ms,average_present_ms,one_percent_low_fps,pacing_stddev_ms,max_present_interval_ms,pacing_spikes_total,capture_to_present_return_ms,overwritten_total,repeated_timestamp_total,missing_timestamp_total,capped_total,capture_width,capture_height,preview_width,preview_height,fps_limit,vsync,sync_interval,tearing_requested,target_monitor,preview_monitor,target_refresh_hz,preview_refresh_hz,app_cpu_percent,eden_cpu_percent,app_working_set_mb,eden_working_set_mb,app_gpu_max_engine_percent,eden_gpu_max_engine_percent,previous_logging_work_ms,previous_write_flush_ms,events_dropped_total,target_minimized,callback_gap_s\n";
std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> result;
    std::istringstream s(line); std::string v;
    while (std::getline(s, v, ',')) result.push_back(v);
    return result;
}
double number(const std::string& s) {
    std::size_t end = 0; double v = std::stod(s, &end);
    if (end != s.size() || !std::isfinite(v)) throw std::runtime_error("Non-finite or malformed numeric metric");
    return v;
}
void limit_utf8(std::string& value, std::size_t limit) {
    std::size_t length = std::min(value.size(), limit);
    while (length && length < value.size() && (static_cast<unsigned char>(value[length]) & 0xC0) == 0x80) --length;
    value.resize(length);
}
}

struct DiagnosticSession::Impl {
    DiagnosticConfig config;
    std::shared_ptr<FrameMetrics> metrics;
    std::shared_ptr<CaptureStats> stats;
    std::shared_ptr<DiagnosticRuntime> runtime;
    std::filesystem::path path;
    std::thread worker;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::pair<std::string, std::string>> events;
    std::atomic<bool> running{false}, failure{false};
    bool stopping = false;
    std::string reason = "session stopped";
    std::atomic<std::uint64_t> lost{0};
    HANDLE storage_lock = INVALID_HANDLE_VALUE;
    ~Impl() { if (storage_lock != INVALID_HANDLE_VALUE) CloseHandle(storage_lock); }

    void record() noexcept {
        try {
            const auto origin = Clock::now();
            auto previous_sample = origin, last_callback_seen = origin;
            std::uint64_t previous_callbacks = 0;
            double previous_work = 0, previous_write = 0;
            std::ofstream csv(path / "metrics.csv", std::ios::binary), event_file(path / "events.jsonl", std::ios::binary);
            if (!csv || !event_file) throw std::runtime_error("Cannot open diagnostic output");
            csv << csv_header << std::fixed << std::setprecision(4);
            ProcessUsage app(GetCurrentProcessId()), eden(config.target_pid);
            app.sample(); eden.sample();
            GpuUsage gpu;
            auto displays = discover_displays(false);
            std::string old_displays, old_configuration;
            std::uintmax_t written = std::filesystem::file_size(path / "metadata.json") + std::char_traits<char>::length(csv_header);
            auto write_event = [&](const std::string& kind, const std::string& detail) {
                std::string line = "{\"utc\":" + json_string(utc()) + ",\"elapsed_s\":" +
                    std::to_string(std::chrono::duration<double>(Clock::now() - origin).count()) +
                    ",\"kind\":" + json_string(kind) + ",\"detail\":" + json_string(detail) + "}\n";
                event_file << line; written += line.size();
            };
            write_event("recording_started", "Declared game label; no game-memory inspection. Visually changed FPS unavailable.");
            auto next = origin + std::chrono::seconds(1);
            unsigned sample_index = 0;
            while (true) {
                std::deque<std::pair<std::string, std::string>> pending;
                bool done;
                {
                    std::unique_lock lock(mutex);
                    wake.wait_until(lock, next, [&] { return stopping || !events.empty(); });
                    pending.swap(events); done = stopping;
                }
                const auto begin = Clock::now();
                for (const auto& e : pending) {
                    if (written + 8192 >= config.session_limit_bytes) break;
                    write_event(e.first, e.second);
                }
                if (done && begin - previous_sample < std::chrono::milliseconds(10)) {
                    write_event("session_end", reason + "; events_dropped=" + std::to_string(lost.load()));
                    break;
                }
                if (written + 8192 >= config.session_limit_bytes) {
                    write_event("session_end", "storage quota reached; capture continues without logging");
                    break;
                }
                if (!done && begin < next) { event_file.flush(); continue; }
                const auto [now, frequency] = qpc();
                const auto m = metrics->snapshot(now, frequency);
                const double elapsed = std::chrono::duration<double>(begin - origin).count();
                const double interval = std::chrono::duration<double>(begin - previous_sample).count();
                previous_sample = begin;
                if (m.callback_total != previous_callbacks) last_callback_seen = begin;
                previous_callbacks = m.callback_total;
                if (sample_index++ % 5 == 0) displays = discover_displays(false);
                const auto inventory = displays_json(displays);
                if (inventory != old_displays) { write_event("displays", inventory); old_displays = inventory; }
                const auto target_monitor = window_display(config.target), preview_monitor = window_display(config.preview);
                double target_hz = 0, preview_hz = 0;
                for (const auto& d : displays) {
                    if (d.device == target_monitor) target_hz = d.refresh_hz;
                    if (d.device == preview_monitor) preview_hz = d.refresh_hz;
                }
                const auto limit = static_cast<FpsLimit>(runtime->fps_limit.load());
                const auto vsync = static_cast<VSyncMode>(runtime->vsync.load());
                const auto policy = present_policy(vsync, config.tearing_supported);
                const auto configuration = utf8(target_monitor) + " -> " + utf8(preview_monitor) + "; limit=" +
                    utf8(fps_limit_name(limit)) + "; vsync=" + utf8(vsync_name(vsync)) + "; dimensions=" +
                    std::to_string(stats->capture_width.load()) + "x" + std::to_string(stats->capture_height.load()) +
                    " -> " + std::to_string(runtime->output_width.load()) + "x" + std::to_string(runtime->output_height.load());
                if (configuration != old_configuration) { write_event("configuration", configuration); old_configuration = configuration; }
                const auto [app_cpu, app_mb] = app.sample();
                const auto [eden_cpu, eden_mb] = eden.sample();
                const auto [app_gpu, eden_gpu] = gpu.sample(config.target_pid);
                std::ostringstream row;
                row << std::fixed << std::setprecision(4) << elapsed << ',' << interval << ',' << m.callback_fps << ','
                    << m.distinct_capture_fps << ',' << m.presented_fps << ',' << m.callback_total << ',' << m.distinct_capture_total << ','
                    << m.presented_total << ',' << m.average_source_ms << ',' << m.average_callback_ms << ',' << m.average_present_ms << ','
                    << m.one_percent_low_fps << ',' << m.pacing_stddev_ms << ',' << m.max_present_interval_ms << ',' << m.pacing_spikes_total << ','
                    << m.average_capture_to_present_ms << ',' << stats->overwritten.load() << ',' << m.repeated_timestamp_total << ','
                    << m.missing_timestamp_total << ',' << m.capped_total << ',' << stats->capture_width.load() << ',' << stats->capture_height.load() << ','
                    << runtime->output_width.load() << ',' << runtime->output_height.load() << ',' << utf8(fps_limit_name(limit)) << ','
                    << utf8(vsync_name(vsync)) << ',' << policy.sync_interval << ',' << policy.allow_tearing << ','
                    << utf8(target_monitor) << ',' << utf8(preview_monitor) << ',' << target_hz << ',' << preview_hz << ','
                    << app_cpu << ',' << eden_cpu << ',' << app_mb << ',' << eden_mb << ',' << app_gpu << ',' << eden_gpu << ','
                    << previous_work << ',' << previous_write << ',' << lost.load() << ',' << (IsIconic(config.target) ? 1 : 0) << ','
                    << std::chrono::duration<double>(begin - last_callback_seen).count() << '\n';
                const auto write_begin = Clock::now();
                const auto line = row.str(); csv << line; written += line.size();
                csv.flush(); event_file.flush();
                if (!csv || !event_file) throw std::runtime_error("Diagnostic write/flush failed");
                previous_write = std::chrono::duration<double, std::milli>(Clock::now() - write_begin).count();
                previous_work = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
                if (done) { write_event("session_end", reason + "; events_dropped=" + std::to_string(lost.load())); break; }
                next += std::chrono::seconds(1);
                if (next < Clock::now()) next = Clock::now() + std::chrono::seconds(1);
            }
            csv.flush(); event_file.flush();
            if (!csv || !event_file) throw std::runtime_error("Diagnostic final flush failed");
            csv.close(); event_file.close();
            analyze_session(path);
        } catch (const std::exception& e) {
            failure = true;
            // No repeated retries or unbounded error output if storage fails.
            OutputDebugStringA((std::string("EmuScale diagnostics: ") + e.what()).c_str());
        }
        running = false;
    }
};
DiagnosticSession::DiagnosticSession() : impl_(std::make_unique<Impl>()) {}
DiagnosticSession::~DiagnosticSession() { stop("application shutdown"); }
void DiagnosticSession::start(const std::filesystem::path& root, DiagnosticConfig config,
    std::shared_ptr<FrameMetrics> metrics, std::shared_ptr<CaptureStats> stats,
    std::shared_ptr<DiagnosticRuntime> runtime) {
    if (impl_->worker.joinable()) throw std::logic_error("Diagnostic session already started");
    std::filesystem::create_directories(root);
    // Exclusive file handle coordinates storage accounting between processes; never deletes old logs.
    impl_->storage_lock = CreateFileW((root / ".recorder.lock").c_str(), GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN, nullptr);
    if (impl_->storage_lock == INVALID_HANDLE_VALUE) throw std::runtime_error("Another recorder uses this diagnostic directory, or it is not writable");
    std::uintmax_t used = 0;
    for (const auto& e : std::filesystem::recursive_directory_iterator(root)) if (e.is_regular_file()) used += e.file_size();
    if (used >= config.storage_limit_bytes || config.storage_limit_bytes - used < 64 * 1024)
        throw std::runtime_error("Diagnostic storage quota reached; archive existing logs before recording");
    config.session_limit_bytes = std::min(config.session_limit_bytes, config.storage_limit_bytes - used);
    if (config.session_limit_bytes < 64 * 1024) throw std::runtime_error("Session quota must allow at least 64 KiB");
    auto name = utc();
    std::replace(name.begin(), name.end(), ':', '-');
    impl_->path = root / (name + "_" + std::to_string(GetCurrentProcessId()));
    if (!std::filesystem::create_directory(impl_->path)) throw std::runtime_error("Diagnostic directory already exists");
    std::ofstream metadata(impl_->path / "metadata.json", std::ios::binary);
    const auto initial_displays = discover_displays();
    metadata << "{\n\"schema_version\":1,\"app_version\":\"0.1.2\",\"started_utc\":" << json_string(utc())
        << ",\"game_label\":" << json_string(config.game) << ",\"game_label_source\":\"user declaration\""
        << ",\"scenario\":" << json_string(config.scenario) << ",\"optimizer\":" << json_string(config.optimizer)
        << ",\"scene\":" << json_string(config.scene) << ",\"configuration_ready\":" << (config.ready ? "true" : "false")
        << ",\"duration_seconds\":" << config.duration_seconds << ",\"target_pid\":" << config.target_pid
        << ",\"target_hwnd\":" << reinterpret_cast<std::uintptr_t>(config.target)
        << ",\"target_process\":" << json_string(utf8(config.target_process)) << ",\"target_title\":" << json_string(utf8(config.target_title))
        << ",\"os\":\"Windows\",\"gpu\":" << json_string(utf8(config.gpu)) << ",\"baseline_expected_game_fps\":30,\"expected_game_fps_is_measurement\":false"
        << ",\"nx_settings_modified\":false,\"visual_change_measurement\":\"unavailable; no readback\""
        << ",\"presented_fps_definition\":\"successful DXGI Present calls, not physical display scanout\""
        << ",\"delay_definition\":\"WGC compositor timestamp to DXGI Present return; not input latency\""
        << ",\"gpu_metric\":\"PDH maximum engine utilization per process across adapters; -1 unavailable\""
        << ",\"cpu_metric\":\"process CPU percent normalized over all logical processors; -1 unavailable\""
        << ",\"spike_threshold_ms\":50,\"session_limit_bytes\":" << config.session_limit_bytes
        << ",\"storage_limit_bytes\":" << config.storage_limit_bytes
        << ",\"active_monitor_count\":" << initial_displays.size()
        << ",\"displays\":" << displays_json(initial_displays) << "\n}\n";
    metadata.flush();
    if (!metadata) throw std::runtime_error("Cannot write diagnostic metadata");
    impl_->config = std::move(config); impl_->metrics = std::move(metrics); impl_->stats = std::move(stats); impl_->runtime = std::move(runtime);
    impl_->running = true;
    impl_->worker = std::thread([this] { impl_->record(); });
}
void DiagnosticSession::event(std::string kind, std::string detail) {
    if (!impl_->running) return;
    std::lock_guard lock(impl_->mutex);
    if (impl_->events.size() >= 128) { ++impl_->lost; return; }
    limit_utf8(kind, 64); limit_utf8(detail, 512);
    impl_->events.emplace_back(std::move(kind), std::move(detail)); impl_->wake.notify_one();
}
void DiagnosticSession::stop(std::string reason) {
    if (!impl_->worker.joinable()) return;
    { std::lock_guard lock(impl_->mutex); impl_->reason = std::move(reason); impl_->stopping = true; }
    impl_->wake.notify_one(); impl_->worker.join();
    if (impl_->storage_lock != INVALID_HANDLE_VALUE) { CloseHandle(impl_->storage_lock); impl_->storage_lock = INVALID_HANDLE_VALUE; }
}
bool DiagnosticSession::active() const { return impl_->running; }
bool DiagnosticSession::failed() const { return impl_->failure; }
std::filesystem::path DiagnosticSession::directory() const { return impl_->path; }

std::string analyze_session(const std::filesystem::path& directory, bool write_summary) {
    const auto path = directory / "metrics.csv";
    if (!std::filesystem::exists(path) || std::filesystem::file_size(path) > 32 * 1024 * 1024)
        throw std::runtime_error("Missing metrics.csv or analyzer input exceeds 32 MiB");
    std::ifstream file(path);
    std::string line;
    std::getline(file, line);
    if (line + '\n' != csv_header) throw std::runtime_error("Unsupported diagnostic CSV schema");
    const auto keys = split(line);
    std::map<std::string, std::size_t> index;
    for (std::size_t i = 0; i < keys.size(); ++i) index[keys[i]] = i;
    std::map<std::string, double> previous;
    std::size_t count = 0, invalid = 0, interruptions = 0, gaps = 0, mismatch = 0, capped = 0, minimized = 0, stable = 0, partial = 0;
    double last_elapsed = 0, callback = 0, distinct = 0, presented = 0, max_interval = 0, max_work = 0, max_write = 0;
    double max_delay = -1, last_overwrites = 0, last_repeats = 0, last_spikes = 0, last_lost = 0, max_cpu = -1;
    double weighted_cpu = 0, cpu_seconds = 0, total_work = 0;
    std::string last_configuration;
    std::size_t changes = 0;
    while (std::getline(file, line)) {
        if (line.size() > 8192) throw std::runtime_error("Oversized metrics row");
        auto fields = split(line);
        if (fields.size() != keys.size()) { ++invalid; continue; }
        try {
            auto v = [&](const std::string& key) { return number(fields.at(index.at(key))); };
            // Validate every numeric field, not only values used by summary heuristics.
            for (std::size_t i = 0; i < fields.size(); ++i) {
                if (keys[i] == "fps_limit" || keys[i] == "vsync" || keys[i] == "target_monitor" || keys[i] == "preview_monitor") continue;
                const double n = number(fields[i]);
                const bool optional = keys[i].find("cpu_percent") != std::string::npos || keys[i].find("working_set") != std::string::npos || keys[i].find("gpu_max") != std::string::npos || keys[i] == "capture_to_present_return_ms";
                if (n < 0 && !(optional && n == -1)) throw std::runtime_error("Negative metric");
                if (keys[i].ends_with("_total") && std::floor(n) != n) throw std::runtime_error("Non-integral cumulative counter");
                if ((keys[i].find("cpu_percent") != std::string::npos || keys[i].find("gpu_max") != std::string::npos) && n > 100.01)
                    throw std::runtime_error("Utilization exceeds 100 percent");
            }
            if (fields[index["vsync"]] != "Auto (On)" && fields[index["vsync"]] != "On" && fields[index["vsync"]] != "Off") throw std::runtime_error("Invalid VSync mode");
            bool known_limit = false;
            for (int n = 0; n <= 8; ++n) if (fields[index["fps_limit"]] == utf8(fps_limit_name(static_cast<FpsLimit>(n)))) known_limit = true;
            if (!known_limit) throw std::runtime_error("Invalid FPS limit");
            if (v("distinct_total") + v("repeated_timestamp_total") + v("missing_timestamp_total") > v("callback_total")) throw std::runtime_error("Frame totals exceed callbacks");
            if (v("elapsed_s") <= last_elapsed || v("sample_interval_s") <= 0) throw std::runtime_error("Non-monotonic sample time");
            for (const auto* key : {"callback_total", "distinct_total", "presented_total", "overwritten_total", "repeated_timestamp_total", "missing_timestamp_total", "pacing_spikes_total", "events_dropped_total", "capped_total"})
                if (previous.contains(key) && v(key) < previous[key]) throw std::runtime_error("Counter regressed");
            for (const auto* key : {"callback_total", "distinct_total", "presented_total", "overwritten_total", "repeated_timestamp_total", "missing_timestamp_total", "pacing_spikes_total", "events_dropped_total", "capped_total"}) previous[key] = v(key);
            last_elapsed = v("elapsed_s"); ++count;
            // A final sub-second row preserves counters but would distort a rate
            // mean and amplify coarse GetProcessTimes rounding during teardown.
            if (v("sample_interval_s") >= .5) {
                ++stable;
                callback += v("callback_fps"); distinct += v("distinct_capture_fps"); presented += v("presented_fps");
                max_cpu = std::max(max_cpu, v("app_cpu_percent"));
                if (v("app_cpu_percent") >= 0) { weighted_cpu += v("app_cpu_percent") * v("sample_interval_s"); cpu_seconds += v("sample_interval_s"); }
            } else ++partial;
            if (v("callback_fps") == 0) ++interruptions;
            if (v("sample_interval_s") > 1.5) ++gaps;
            if (v("distinct_capture_fps") > 0 && v("presented_fps") < v("distinct_capture_fps") * .85) ++mismatch;
            if (fields[index["fps_limit"]] != "Unlocked" && fields[index["fps_limit"]] != "Auto") ++capped;
            if (v("target_minimized")) ++minimized;
            max_interval = std::max(max_interval, v("max_present_interval_ms"));
            max_work = std::max(max_work, v("previous_logging_work_ms")); max_write = std::max(max_write, v("previous_write_flush_ms"));
            max_delay = std::max(max_delay, v("capture_to_present_return_ms"));
            total_work += v("previous_logging_work_ms");
            last_overwrites = v("overwritten_total"); last_repeats = v("repeated_timestamp_total"); last_spikes = v("pacing_spikes_total"); last_lost = v("events_dropped_total");
            std::string config;
            for (const auto* key : {"fps_limit", "vsync", "target_monitor", "preview_monitor", "capture_width", "capture_height", "preview_width", "preview_height", "target_refresh_hz", "preview_refresh_hz"}) config += fields[index[key]] + ";";
            if (!last_configuration.empty() && last_configuration != config) ++changes;
            last_configuration = config;
        } catch (const std::exception&) { ++invalid; }
    }
    bool finalized = false, quota = false;
    const auto events_path = directory / "events.jsonl";
    if (std::filesystem::exists(events_path) && std::filesystem::file_size(events_path) <= 32 * 1024 * 1024) {
        std::ifstream events(events_path);
        while (std::getline(events, line)) {
            if (line.find("\"kind\":\"session_end\"") != std::string::npos) finalized = true;
            if (line.find("storage quota reached") != std::string::npos) quota = true;
        }
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << "# EmuScale RTX diagnostic analysis\n\n"
        << "Session: " << utf8(directory.filename().wstring()) << "\n\n"
        << "Validation: " << count << " valid samples, " << invalid << " invalid rows; "
        << (finalized ? "clean session-end event" : "partial session (no session-end event)") << ".\n\n";
    if (count) {
        out << "Recorded duration: " << last_elapsed << " s. Mean one-second callback / timestamp-distinct / presented FPS: "
            << (stable ? callback / stable : 0) << " / " << (stable ? distinct / stable : 0) << " / " << (stable ? presented / stable : 0)
            << ". Rate/CPU summaries exclude " << partial << " final/short intervals under 0.5 s. These are capture/Present measurements, not internal game FPS.\n\n"
            << "Confirmed observations:\n\n- Presentation intervals over 50 ms: " << last_spikes << "; largest observed interval: " << max_interval << " ms.\n"
            << "- Application mailbox overwrites: " << last_overwrites << "; repeated WGC timestamps: " << last_repeats << ".\n"
            << "- Samples with no capture callbacks: " << interruptions << "; sampling intervals over 1.5 s: " << gaps << "; minimized-target samples: " << minimized << ".\n"
            << "- Presentation below 85% of distinct capture FPS: " << mismatch << " samples; numeric cap active: " << capped << " samples.\n"
            << "- Configuration transitions: " << changes << "; dropped diagnostic events: " << last_lost << ".\n"
            << "- Maximum sampled rolling capture-to-Present-return mean: " << max_delay << " ms (not scanout or input latency).\n"
            << "- Largest observed previous logging work / write-and-flush: " << max_work << " / " << max_write
            << " ms; mean sampled logging work: " << total_work / count << " ms; process CPU mean / peak in stable intervals: "
            << (cpu_seconds > 0 ? weighted_cpu / cpu_seconds : -1) << " / " << max_cpu << "%.\n\n"
            << "Evidence-based next steps (causes remain suspected):\n\n";
        if (invalid) out << "- Repair invalid metric rows before treating this session as a benchmark.\n";
        if (changes) out << "- Split comparisons at configuration events; do not compare mixed settings as a single benchmark.\n";
        if (interruptions || gaps) out << "- Correlate interruptions with activity, minimized state, target closure and capture-error events; a source stall or scheduling delay is possible.\n";
        if (mismatch) out << "- Inspect numeric caps, VSync/monitor refresh and mailbox overwrites before attributing the presentation shortfall to GPU cost.\n";
        if (last_spikes) out << "- Correlate >50 ms intervals with loading, travel or stutter markers; fixed-threshold spikes alone do not establish a renderer fault.\n";
        if (max_work > 10 || max_write > 10) out << "- Investigate diagnostic sampling or disk work exceeding 10 ms on the worker.\n";
        else out << "- Observed logging work stayed below 10 ms per sampled cycle. An A/B run with diagnostics disabled is needed to attribute gameplay overhead.\n";
        if (!mismatch && !interruptions) out << "- Capture and presentation tracked in the sampled windows; repeat a matched 60-120 s scene before changing the capture pipeline.\n";
    } else out << "No valid performance samples; collect a session lasting at least one second.\n\n";
    if (quota) out << "\nLogging stopped at its storage quota; capture may have continued. Archive logs before the next recording.\n";
    out << "\nVisually changed FPS and pixel duplicates are unavailable. Distinct timestamps cannot prove image changes. GPU values are maximum per-process engine counters, not total device utilization; -1 means unavailable. Logging work is reported one cycle late; the final cycle is excluded. No causal logging-overhead conclusion is possible without a matched control run.\n";
    if (write_summary) {
        std::ofstream summary(directory / "summary.md", std::ios::binary); summary << out.str(); summary.flush();
        if (!summary) throw std::runtime_error("Cannot write diagnostic summary");
    }
    return out.str();
}
}
