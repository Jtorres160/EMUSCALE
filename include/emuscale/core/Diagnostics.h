#pragma once
#include "emuscale/core/CaptureStats.h"
#include "emuscale/core/FrameMetrics.h"
#include "emuscale/core/FramePacing.h"
#include <windows.h>
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>

namespace emuscale {
struct DiagnosticConfig {
    HWND target = nullptr, preview = nullptr;
    DWORD target_pid = 0;
    std::wstring target_process, target_title, gpu;
    std::string game = "The Legend of Zelda: Tears of the Kingdom (TotK)";
    std::string scenario = "A", optimizer = "disabled", scene = "unspecified";
    bool ready = false, tearing_supported = false;
    int duration_seconds = 0;
    std::uintmax_t session_limit_bytes = 32 * 1024 * 1024;
    std::uintmax_t storage_limit_bytes = 256 * 1024 * 1024;
};
// UI writes these scalar settings; sampling and all diagnostic I/O run on a worker.
struct DiagnosticRuntime {
    std::atomic<int> fps_limit{static_cast<int>(FpsLimit::unlocked)};
    std::atomic<int> vsync{static_cast<int>(VSyncMode::automatic)};
    std::atomic<unsigned> output_width{0}, output_height{0};
};
class DiagnosticSession {
public:
    DiagnosticSession();
    ~DiagnosticSession();
    void start(const std::filesystem::path& root, DiagnosticConfig config,
               std::shared_ptr<FrameMetrics> metrics, std::shared_ptr<CaptureStats> stats,
               std::shared_ptr<DiagnosticRuntime> runtime);
    void event(std::string kind, std::string detail);
    void stop(std::string reason);
    bool active() const;
    bool failed() const;
    std::filesystem::path directory() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Validates saved CSV (including partial sessions) and writes a bounded summary.
std::string analyze_session(const std::filesystem::path& directory, bool write_summary = true);
}
