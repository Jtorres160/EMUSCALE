#include "emuscale/capture/WindowCapture.h"
#include "emuscale/core/CaptureStats.h"
#include "emuscale/core/Benchmark.h"
#include "emuscale/core/FramePacing.h"
#include "emuscale/core/FrameMetrics.h"
#include "emuscale/core/Diagnostics.h"
#include "emuscale/windows/DisplayInventory.h"
#include "emuscale/presentation/PreviewRenderer.h"
#include "emuscale/windows/WindowDiscovery.h"

#include <windows.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <wrl/client.h>
#include <algorithm>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <filesystem>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <string_view>
#include <chrono>

namespace {
constexpr UINT marker_message = WM_APP + 10;
constexpr const char* markers[] = {"Normal exploration", "Combat", "Fast travel", "Menu opened",
    "Loading screen", "Heavy visual effects", "Stuttering observed"};
struct Options {
    bool diagnostics = false, ready = false, list = false, display_info = false;
    std::string scenario = "A", optimizer = "disabled", scene = "unspecified";
    std::uintptr_t hwnd = 0;
    int duration = 0;
    emuscale::FpsLimit limit = emuscale::FpsLimit::unlocked;
    emuscale::VSyncMode vsync = emuscale::VSyncMode::automatic;
};
Options options(int argc, wchar_t** argv) {
    Options out;
    bool optimizer_declared = false;
    for (int i = 1; i < argc; ++i) {
        std::wstring arg(argv[i]);
        if (arg == L"--diagnostics") out.diagnostics = true;
        else if (arg == L"--ready") out.ready = true;
        else if (arg == L"--list-eden") out.list = true;
        else if (arg == L"--display-info") out.display_info = true;
        else if (arg.starts_with(L"--scenario=")) out.scenario = emuscale::utf8(arg.substr(11));
        else if (arg.starts_with(L"--optimizer=")) { out.optimizer = emuscale::utf8(arg.substr(12)); optimizer_declared = true; }
        else if (arg.starts_with(L"--scene=")) out.scene = emuscale::utf8(arg.substr(8));
        else if (arg.starts_with(L"--target-hwnd=")) { std::size_t end = 0; const auto value = arg.substr(14); out.hwnd = std::stoull(value, &end, 0); if (end != value.size() || !out.hwnd) throw std::runtime_error("Invalid HWND"); }
        else if (arg.starts_with(L"--duration=")) { std::size_t end = 0; const auto value = arg.substr(11); out.duration = std::stoi(value, &end); if (end != value.size()) throw std::runtime_error("Invalid duration"); }
        else if (arg.starts_with(L"--limit=")) {
            bool found = false;
            for (int n = 0; n <= 8; ++n) if (arg.substr(8) == emuscale::fps_limit_name(static_cast<emuscale::FpsLimit>(n)) ||
                (emuscale::fps_cap(static_cast<emuscale::FpsLimit>(n)) > 0 && arg.substr(8) == std::to_wstring(emuscale::fps_cap(static_cast<emuscale::FpsLimit>(n))))) {
                out.limit = static_cast<emuscale::FpsLimit>(n); found = true;
            }
            if (!found) throw std::runtime_error("Use --limit=Auto|30|60|90|120|144|165|240|Unlocked");
        } else if (arg.starts_with(L"--vsync=")) {
            const auto value = arg.substr(8);
            if (value == L"Auto") out.vsync = emuscale::VSyncMode::automatic;
            else if (value == L"On") out.vsync = emuscale::VSyncMode::on;
            else if (value == L"Off") out.vsync = emuscale::VSyncMode::off;
            else throw std::runtime_error("Use --vsync=Auto|On|Off");
        } else throw std::runtime_error("Unknown option (use --help)");
    }
    if (out.duration != 0 && out.duration != 30 && out.duration != 60 && out.duration != 120)
        throw std::runtime_error("Duration must be 0 (continuous), 30, 60, or 120 seconds");
    if (out.scenario != "A" && out.scenario != "B" && out.scenario != "C") throw std::runtime_error("Scenario must be A, B, or C");
    if (out.diagnostics && out.scenario != "A" && (!out.ready || !optimizer_declared || out.optimizer.empty() || out.scene == "unspecified"))
        throw std::runtime_error("Tests B/C require --ready, an explicit --optimizer=configuration and --scene=label");
    if (out.diagnostics && out.scenario == "B" && out.optimizer == "disabled") throw std::runtime_error("Test B requires a declared working optimizer configuration");
    if (out.diagnostics && out.scenario == "A" && (out.optimizer != "disabled" || out.limit != emuscale::FpsLimit::unlocked))
        throw std::runtime_error("Baseline A requires optimizer disabled and FPS limit Unlocked");
    if (out.optimizer.size() > 512 || out.scene.size() > 512) throw std::runtime_error("Configuration labels must be <=512 UTF-8 bytes");
    return out;
}
void write_console(std::wstring_view value) {
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!output || output == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    if (WriteConsoleW(output, value.data(), static_cast<DWORD>(value.size()),
                      &written, nullptr)) return;
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return;
    std::string utf8(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        utf8.data(), bytes, nullptr, nullptr);
    WriteFile(output, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
}

struct AppState {
    HWND target = nullptr;
    HWND preview = nullptr;
    HWND controls = nullptr;
    HWND metrics_text = nullptr;
    HWND benchmark_text = nullptr;
    emuscale::PreviewRenderer renderer;
    emuscale::WindowCapture capture;
    std::shared_ptr<emuscale::CaptureStats> stats = std::make_shared<emuscale::CaptureStats>();
    std::shared_ptr<emuscale::FrameMetrics> metrics = std::make_shared<emuscale::FrameMetrics>();
    emuscale::FramePacer pacer;
    emuscale::Benchmark benchmark;
    emuscale::DiagnosticSession diagnostics;
    std::shared_ptr<emuscale::DiagnosticRuntime> diagnostic_runtime = std::make_shared<emuscale::DiagnosticRuntime>();
    bool diagnostic_mode = false;
    int diagnostic_duration = 0;
    std::chrono::steady_clock::time_point capture_started;
    std::int64_t last_presented_source_ticks = 0;
    int monitor_refresh_hz = 0;
    std::wstring status = L"Starting";
    std::wstring error;
};

std::pair<std::int64_t, std::int64_t> qpc() {
    LARGE_INTEGER now{}, frequency{};
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    return {now.QuadPart, frequency.QuadPart};
}

int refresh_rate(HWND window) {
    MONITORINFOEXW monitor{};
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) return 0;
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (!EnumDisplaySettingsW(monitor.szDevice, ENUM_CURRENT_SETTINGS, &mode)) return 0;
    return mode.dmDisplayFrequency > 1 ? static_cast<int>(mode.dmDisplayFrequency) : 0;
}

std::wstring diagnose(const emuscale::MetricsSnapshot& m, int cap, int target,
                      std::uint64_t overwritten) {
    if (m.callback_fps < 5) return L"Waiting for capture data";
    if (m.distinct_capture_fps < m.callback_fps * 0.8)
        return L"Repeated capture timestamps; game image rate unconfirmed";
    if (cap > 0 && m.distinct_capture_fps > cap * 1.1 && m.presented_fps <= cap * 1.1)
        return L"Numeric presentation cap is active";
    if (m.presented_fps < m.distinct_capture_fps * 0.85)
        return overwritten ? L"Preview may lag capture; mailbox frames overwritten" :
                             L"Preview below capture rate; cause unconfirmed";
    if (target > 0 && m.distinct_capture_fps < target * 0.8)
        return L"Source/capture below target; game vs emulator cause unconfirmed";
    return L"Capture and presentation tracking; no bottleneck identified";
}

std::filesystem::path benchmark_directory() {
    wchar_t executable[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
    if (!length || length >= 32768) throw std::runtime_error("Cannot locate executable directory");
    return std::filesystem::path(std::wstring(executable, length)).parent_path() / L"benchmarks";
}

void export_benchmark(AppState& app) {
    if (!app.benchmark.active()) return;
    const auto path = app.benchmark.stop_and_export(benchmark_directory(), app.renderer.gpu_name());
    std::wcout << L"[Benchmark] CSV: " << path.wstring() << L'\n';
    if (app.benchmark_text) SetWindowTextW(app.benchmark_text,
        (L"Saved: " + path.wstring()).c_str());
}

std::wstring hresult_text(long code) {
    std::wostringstream text;
    text << L"HRESULT 0x" << std::hex << static_cast<unsigned long>(code);
    return text.str();
}

void show_frame(AppState& app) {
    auto frame = app.capture.take_frame();
    if (!frame) return;
    const auto size = frame->ContentSize();
    if (size.Width <= 0 || size.Height <= 0) { frame->Close(); return; }
    const auto source_ticks = frame->SystemRelativeTime().count();
    if (source_ticks != 0 && source_ticks == app.last_presented_source_ticks) {
        frame->Close();
        return;
    }
    const auto [now, frequency] = qpc();
    if (!app.pacer.allow(now, frequency)) {
        app.metrics->on_capped();
        frame->Close();
        return;
    }
    auto access = frame->Surface().as<
        ::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(&texture)));
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    const bool presented = app.renderer.present(texture.Get(), static_cast<UINT>(size.Width),
                         static_cast<UINT>(size.Height));
    const auto [presented_at, present_frequency] = qpc();
    if (presented) {
        app.stats->presented.fetch_add(1, std::memory_order_relaxed);
        app.metrics->on_present(presented_at, present_frequency, source_ticks);
    }
    app.last_presented_source_ticks = source_ticks;
    texture.Reset();
    frame->Close();
    frame.reset();
    if (static_cast<UINT>(size.Width) != desc.Width ||
        static_cast<UINT>(size.Height) != desc.Height) {
        app.capture.resize_pool(size);
    }
}

void update_title(AppState& app) {
    app.monitor_refresh_hz = refresh_rate(app.preview);
    app.diagnostic_runtime->output_width = app.renderer.width();
    app.diagnostic_runtime->output_height = app.renderer.height();
    const auto [now, frequency] = qpc();
    const auto measured = app.metrics->snapshot(now, frequency);
    const auto dropped = app.stats->overwritten.load(std::memory_order_relaxed);
    const auto width = app.stats->capture_width.load(std::memory_order_relaxed);
    const auto height = app.stats->capture_height.load(std::memory_order_relaxed);
    const int cap = emuscale::fps_cap(app.pacer.limit());
    const int target = cap ? cap : app.pacer.limit() == emuscale::FpsLimit::automatic ?
        app.monitor_refresh_hz : 0;
    std::wstring title = L"EmuScale RTX v0.1.2 | " + app.status + L" | " +
        app.renderer.gpu_name() + L" | source timestamps " +
        std::to_wstring(static_cast<int>(measured.distinct_capture_fps)) + L" FPS | present " +
        std::to_wstring(static_cast<int>(measured.presented_fps)) + L" FPS | target " +
        (target ? std::to_wstring(target) : L"Unlocked") + L" | " +
        std::to_wstring(width) + L"x" + std::to_wstring(height) + L" -> " +
        std::to_wstring(app.renderer.width()) + L"x" +
        std::to_wstring(app.renderer.height());
    SetWindowTextW(app.preview, title.c_str());
    if (!app.diagnostic_mode) std::wcout << L"[Capture] " << app.renderer.gpu_name() << L" | source timestamps "
               << measured.distinct_capture_fps << L" FPS | callbacks "
               << measured.callback_fps << L" FPS | presented "
               << measured.presented_fps << L" FPS | target "
               << (target ? std::to_wstring(target) : L"Unlocked") << L" | "
               << width << L"x" << height << L" -> "
               << app.renderer.width() << L"x" << app.renderer.height()
               << L" | overwritten " << dropped << L" | repeated timestamps "
               << measured.repeated_timestamp_total << L" | capped " << measured.capped_total
               << L" | VSync " << emuscale::vsync_name(app.renderer.vsync()) << L'\n';
    if (app.metrics_text) {
        std::wostringstream lines;
        lines << std::fixed << std::setprecision(1)
              << L"Distinct WGC timestamps: " << measured.distinct_capture_fps << L" FPS\r\n"
              << L"Capture callbacks: " << measured.callback_fps << L" FPS\r\n"
              << L"Presented: " << measured.presented_fps << L" FPS (target "
              << (target ? std::to_wstring(target) : L"Unlocked") << L")\r\n"
              << L"Avg intervals: source " << measured.average_source_ms
              << L" ms | callback " << measured.average_callback_ms
              << L" ms | present " << measured.average_present_ms << L" ms\r\n"
              << L"1% low: ";
        if (measured.one_percent_low_fps > 0) lines << measured.one_percent_low_fps << L" FPS";
        else lines << L"insufficient samples";
        lines << L" | pacing stddev " << measured.pacing_stddev_ms << L" ms\r\n"
              << L"Overwritten " << dropped << L" | repeated timestamps "
              << measured.repeated_timestamp_total << L" | capped " << measured.capped_total
              << L" | generated 0\r\n"
              << L"GPU: " << app.renderer.gpu_name() << L"\r\n"
              << L"Capture " << width << L"x" << height << L" -> output "
              << app.renderer.width() << L"x" << app.renderer.height()
              << L" | VSync " << emuscale::vsync_name(app.renderer.vsync()) << L"\r\n"
              << L"Diagnostic: " << diagnose(measured, cap, target, dropped)
              << L"\r\nEden: " << emuscale::window_display(app.target) << L" | Preview: " << emuscale::window_display(app.preview)
              << L"\r\nRecorder: " << (app.diagnostics.failed() ? L"ERROR (see console)" : app.diagnostics.active() ? L"recording" : L"inactive/quota")
              << L" | spikes >50 ms: " << measured.pacing_spikes_total;
        SetWindowTextW(app.metrics_text, lines.str().c_str());
    }
    if (app.benchmark.active()) {
        app.benchmark.add({app.benchmark.elapsed(now, frequency), measured, target,
            dropped, width, height, app.renderer.width(), app.renderer.height(),
            app.renderer.vsync()});
        if (app.benchmark_text) {
            std::wstring state = L"Benchmark: " + std::to_wstring(static_cast<int>(
                app.benchmark.elapsed(now, frequency))) + L" / " +
                std::to_wstring(app.benchmark.duration_seconds()) + L" s";
            SetWindowTextW(app.benchmark_text, state.c_str());
        }
        if (app.benchmark.expired(now, frequency)) export_benchmark(app);
    }
}

constexpr int fps_combo_id = 2001;
constexpr int vsync_combo_id = 2002;
constexpr int duration_combo_id = 2003;
constexpr int benchmark_start_id = 2004;
constexpr int benchmark_stop_id = 2005;
constexpr int controls_hide_id = 2006;

LRESULT CALLBACK controls_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* app = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!app) return DefWindowProcW(window, message, wparam, lparam);
    try {
        if (message == WM_CLOSE) { ShowWindow(window, SW_HIDE); return 0; }
        if (message == WM_COMMAND) {
            const int id = LOWORD(wparam);
            const int event = HIWORD(wparam);
            if (id == fps_combo_id && event == CBN_SELCHANGE) {
                const int selected = static_cast<int>(SendMessageW(
                    reinterpret_cast<HWND>(lparam), CB_GETCURSEL, 0, 0));
                if (selected >= 0 && selected <= 8) {
                    app->pacer.set_limit(static_cast<emuscale::FpsLimit>(selected));
                    app->diagnostic_runtime->fps_limit = selected;
                    app->diagnostics.event("fps_limit", emuscale::utf8(emuscale::fps_limit_name(app->pacer.limit())));
                    std::wcout << L"[Settings] FPS limit: "
                               << emuscale::fps_limit_name(app->pacer.limit()) << L'\n';
                }
                return 0;
            }
            if (id == vsync_combo_id && event == CBN_SELCHANGE) {
                const int selected = static_cast<int>(SendMessageW(
                    reinterpret_cast<HWND>(lparam), CB_GETCURSEL, 0, 0));
                if (selected >= 0 && selected <= 2) {
                    app->renderer.set_vsync(static_cast<emuscale::VSyncMode>(selected));
                    app->diagnostic_runtime->vsync = selected;
                    app->diagnostics.event("vsync", emuscale::utf8(emuscale::vsync_name(app->renderer.vsync())));
                    std::wcout << L"[Settings] VSync: "
                               << emuscale::vsync_name(app->renderer.vsync()) << L'\n';
                }
                return 0;
            }
            if (id == benchmark_start_id && event == BN_CLICKED) {
                HWND combo = GetDlgItem(window, duration_combo_id);
                const int selected = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
                const int duration = selected == 1 ? 60 : selected == 2 ? 120 : 30;
                app->benchmark.start(qpc().first, duration);
                SetWindowTextW(app->benchmark_text, L"Benchmark running...");
                std::wcout << L"[Benchmark] Started " << duration << L" seconds\n";
                return 0;
            }
            if (id == benchmark_stop_id && event == BN_CLICKED) {
                export_benchmark(*app);
                return 0;
            }
            if (id == controls_hide_id && event == BN_CLICKED) {
                ShowWindow(window, SW_HIDE);
                SetFocus(app->preview);
                return 0;
            }
            if (id >= 2100 && id < 2107 && event == BN_CLICKED) {
                app->diagnostics.event("activity", markers[id - 2100]);
                return 0;
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "Controls error: " << error.what() << '\n';
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

void create_controls(AppState& app, HINSTANCE instance) {
    WNDCLASSEXW kind{};
    kind.cbSize = sizeof(kind);
    kind.lpfnWndProc = controls_proc;
    kind.hInstance = instance;
    kind.lpszClassName = L"EmuScaleRTXControls";
    kind.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&kind)) throw std::runtime_error("Controls RegisterClassEx failed");
    app.controls = CreateWindowExW(0, kind.lpszClassName,
        L"EmuScale RTX Settings", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
        WS_MINIMIZEBOX, 80, 80, 650, 505, nullptr,
        nullptr, instance, &app);
    if (!app.controls) throw std::runtime_error("Controls CreateWindowEx failed");
    auto label = [&](const wchar_t* value, int x, int y, int width) {
        CreateWindowExW(0, L"STATIC", value, WS_CHILD | WS_VISIBLE,
            x, y, width, 22, app.controls, nullptr, instance, nullptr);
    };
    auto combo = [&](int id, int x, int y, int width) {
        return CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE |
            CBS_DROPDOWNLIST | WS_VSCROLL, x, y, width, 240, app.controls,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    };
    label(L"FPS Limit", 16, 16, 120);
    HWND fps = combo(fps_combo_id, 16, 40, 185);
    for (int i = 0; i <= 8; ++i) SendMessageW(fps, CB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(emuscale::fps_limit_name(static_cast<emuscale::FpsLimit>(i))));
    SendMessageW(fps, CB_SETCURSEL, static_cast<WPARAM>(app.pacer.limit()), 0);
    label(L"VSync", 220, 16, 120);
    HWND vsync = combo(vsync_combo_id, 220, 40, 150);
    for (int i = 0; i <= 2; ++i) SendMessageW(vsync, CB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(emuscale::vsync_name(static_cast<emuscale::VSyncMode>(i))));
    SendMessageW(vsync, CB_SETCURSEL, static_cast<WPARAM>(app.renderer.vsync()), 0);
    label(L"Benchmark duration", 390, 16, 180);
    HWND duration = combo(duration_combo_id, 390, 40, 130);
    for (const wchar_t* value : {L"30 seconds", L"60 seconds", L"120 seconds"})
        SendMessageW(duration, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
    SendMessageW(duration, CB_SETCURSEL, 0, 0);
    CreateWindowExW(0, L"BUTTON", L"Start Benchmark", WS_CHILD | WS_VISIBLE |
        BS_PUSHBUTTON, 16, 84, 145, 28, app.controls,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(benchmark_start_id)), instance, nullptr);
    CreateWindowExW(0, L"BUTTON", L"Stop and Export CSV", WS_CHILD | WS_VISIBLE |
        BS_PUSHBUTTON, 173, 84, 165, 28, app.controls,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(benchmark_stop_id)), instance, nullptr);
    CreateWindowExW(0, L"BUTTON", L"Hide (F1)", WS_CHILD | WS_VISIBLE |
        BS_PUSHBUTTON, 535, 84, 90, 28, app.controls,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(controls_hide_id)), instance, nullptr);
    app.benchmark_text = CreateWindowExW(0, L"STATIC", L"Benchmark idle",
        WS_CHILD | WS_VISIBLE, 16, 122, 610, 24, app.controls, nullptr, instance, nullptr);
    app.metrics_text = CreateWindowExW(0, L"STATIC", L"Waiting for frames...",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 16, 151, 610, 230,
        app.controls, nullptr, instance, nullptr);
    label(L"Distinct timestamps are not proof of distinct game images. F1 reopens this panel.",
          16, 383, 620);
    for (int i = 0; i < 7; ++i) {
        const std::wstring labels[] = {L"Explore", L"Combat", L"Travel", L"Menu", L"Loading", L"Effects", L"Stutter"};
        CreateWindowExW(0, L"BUTTON", labels[i].c_str(), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            16 + i * 87, 414, 82, 28, app.controls, reinterpret_cast<HMENU>(static_cast<INT_PTR>(2100 + i)), instance, nullptr);
    }
    if (!app.diagnostic_mode) ShowWindow(app.controls, SW_SHOW);
}

LRESULT CALLBACK preview_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* app = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!app) return DefWindowProcW(window, message, wparam, lparam);
    try {
        switch (message) {
        case marker_message:
            if (wparam < std::size(markers)) app->diagnostics.event("activity", markers[wparam]);
            return 0;
        case emuscale::capture_frame_message:
            show_frame(*app);
            return 0;
        case emuscale::capture_closed_message:
            app->status = L"Target closed";
            DestroyWindow(window);
            return 0;
        case emuscale::capture_failed_message:
            app->status = L"Capture interrupted";
            app->error = L"Frame acquisition failed (" +
                hresult_text(app->stats->capture_hresult.load(std::memory_order_relaxed)) + L")";
            DestroyWindow(window);
            return 0;
        case WM_SIZE:
            if (wparam != SIZE_MINIMIZED)
                app->renderer.resize(LOWORD(lparam), HIWORD(lparam));
            app->diagnostic_runtime->output_width = app->renderer.width();
            app->diagnostic_runtime->output_height = app->renderer.height();
            return 0;
        case WM_KEYDOWN:
            if (wparam == VK_F1 && app->controls) {
                ShowWindow(app->controls, SW_SHOW);
                SetForegroundWindow(app->controls);
            }
            return 0;
        case WM_TIMER:
            if (app->diagnostic_mode && app->diagnostic_duration > 0 &&
                std::chrono::steady_clock::now() - app->capture_started >= std::chrono::seconds(app->diagnostic_duration)) {
                app->status = L"Timed session complete";
                DestroyWindow(window);
                return 0;
            }
            if (app->stats->capture_failed.load(std::memory_order_relaxed)) {
                app->status = L"Capture interrupted";
                app->error = L"Frame acquisition failed (" +
                    hresult_text(app->stats->capture_hresult.load(std::memory_order_relaxed)) + L")";
                DestroyWindow(window);
            } else if (app->stats->target_closed.load(std::memory_order_relaxed) ||
                       !IsWindow(app->target)) {
                app->status = L"Target closed";
                DestroyWindow(window);
            } else {
                update_title(*app);
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            KillTimer(window, 1);
            try { export_benchmark(*app); } catch (...) {}
            app->capture.stop();
            app->diagnostics.event("capture_stopped", emuscale::utf8(app->status + L" " + app->error));
            app->diagnostics.stop(emuscale::utf8(app->status + L" " + app->error));
            if (!app->diagnostics.directory().empty()) std::wcout << L"[Diagnostics] " << app->diagnostics.directory().wstring() << L"\n";
            if (app->diagnostics.failed()) std::cerr << "Diagnostics failed: inspect session files and disk availability\n";
            if (app->controls && IsWindow(app->controls)) DestroyWindow(app->controls);
            PostQuitMessage(0);
            return 0;
        }
    } catch (const winrt::hresult_error& error) {
        app->error = error.message().c_str();
        app->status = L"Capture failed";
        DestroyWindow(window);
        return 0;
    } catch (const std::exception& error) {
        app->error.assign(error.what(), error.what() + std::char_traits<char>::length(error.what()));
        app->status = L"Capture failed";
        DestroyWindow(window);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

int run(const Options& opt) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    if (opt.display_info) { std::cout << emuscale::displays_json(emuscale::discover_displays()) << '\n'; return 0; }
    auto windows = (opt.diagnostics || opt.list) ? emuscale::eden_windows() : emuscale::discover_windows();
    if (windows.empty()) {
        std::wcout << L"No visible application windows found. Start an emulator and retry.\n";
        return 1;
    }
    write_console(opt.diagnostics || opt.list ? L"Capturable Eden process windows:\r\n" : L"Select a window to capture (emulators listed first):\r\n");
    for (std::size_t i = 0; i < windows.size(); ++i) {
        const auto& item = windows[i];
        std::wstring line = std::to_wstring(i + 1) + L". " + item.title + L" | " +
            item.process_name + L" | PID " + std::to_wstring(item.process_id) +
            L" | HWND " + std::to_wstring(reinterpret_cast<std::uintptr_t>(item.handle)) + L" | " + emuscale::window_display(item.handle) +
            (item.minimized ? L" | minimized (restore first)" : L"") + L"\r\n";
        write_console(line);
    }
    if (opt.list) return 0;
    std::size_t selection = 1;
    if (opt.diagnostics && opt.hwnd) {
        const auto found = std::find_if(windows.begin(), windows.end(), [&](const auto& w) { return reinterpret_cast<std::uintptr_t>(w.handle) == opt.hwnd; });
        if (found == windows.end()) throw std::runtime_error("Requested HWND is not a visible, restored Eden process window");
        selection = static_cast<std::size_t>(found - windows.begin()) + 1;
    } else if (!opt.diagnostics || windows.size() != 1) {
        if (opt.diagnostics) throw std::runtime_error("Multiple plausible Eden windows. Review --list-eden and select --target-hwnd=<HWND>; no capture started");
        write_console(L"Number: ");
        if (!(std::wcin >> selection) || selection < 1 || selection > windows.size()) throw std::runtime_error("Invalid selection");
    }
    const HWND target = windows[selection - 1].handle;
    if (opt.diagnostics && !opt.hwnd && !emuscale::declared_totk_title(windows[selection - 1].title))
        throw std::runtime_error("Eden found, but its title does not identify the declared TotK game. Review --list-eden and explicitly confirm with --target-hwnd=<HWND>");
    if (!emuscale::capturable_window(target))
        throw std::runtime_error("The selected window is closed, hidden, or minimized");
    std::wcout << L"[Target] " << windows[selection - 1].title << L" | "
               << windows[selection - 1].process_name << L" | PID "
               << windows[selection - 1].process_id << L'\n';

    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = preview_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = L"EmuScaleRTXPreview";
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&window_class)) throw std::runtime_error("RegisterClassEx failed");

    AppState app;
    app.target = target;
    const HWND foreground_before = GetForegroundWindow();
    RECT target_bounds_before{}; GetWindowRect(target, &target_bounds_before);
    app.diagnostic_mode = opt.diagnostics;
    app.diagnostic_duration = opt.duration;
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT, width = 1280, height = 760;
    if (opt.diagnostics) {
        const auto source_monitor = MonitorFromWindow(target, MONITOR_DEFAULTTONEAREST);
        for (const auto& d : emuscale::discover_displays(false)) if (d.handle != source_monitor) {
            x = d.work.left + 20; y = d.work.top + 20;
            width = std::min<int>(1000, d.work.right - x); height = std::min<int>(650, d.work.bottom - y); break;
        }
        app.pacer.set_limit(opt.limit);
        app.renderer.set_vsync(opt.vsync);
    }
    app.preview = CreateWindowExW(0, window_class.lpszClassName, L"EmuScale RTX v0.1.2",
        WS_OVERLAPPEDWINDOW, x, y, width, height,
        nullptr, nullptr, instance, &app);
    if (!app.preview) throw std::runtime_error("CreateWindowEx failed");
    try {
        app.renderer.initialize(app.preview);
        app.monitor_refresh_hz = refresh_rate(app.preview);
        std::wcout << L"[Preview] D3D11 swap chain initialized on "
                   << app.renderer.gpu_name() << L" | tearing "
                   << (app.renderer.tearing_supported() ? L"supported" : L"unavailable") << L'\n';
        create_controls(app, instance);
        if (opt.diagnostics) {
            app.diagnostic_runtime->fps_limit = static_cast<int>(app.pacer.limit());
            app.diagnostic_runtime->vsync = static_cast<int>(app.renderer.vsync());
            app.diagnostic_runtime->output_width = app.renderer.width();
            app.diagnostic_runtime->output_height = app.renderer.height();
            emuscale::DiagnosticConfig config;
            const auto& selected = windows[selection - 1];
            config.target = target; config.preview = app.preview; config.target_pid = selected.process_id;
            config.target_process = selected.process_name; config.target_title = selected.title; config.gpu = app.renderer.gpu_name();
            config.scenario = opt.scenario; config.optimizer = opt.optimizer; config.scene = opt.scene; config.ready = opt.ready;
            config.duration_seconds = opt.duration; config.tearing_supported = app.renderer.tearing_supported();
            app.diagnostics.start(benchmark_directory().parent_path() / L"diagnostics", config, app.metrics, app.stats, app.diagnostic_runtime);
            std::wcout << L"[Diagnostics] Recording TotK to " << app.diagnostics.directory().wstring() << L"\n";
        }
        // Recheck process identity immediately before creating the HWND capture item.
        DWORD current_pid = 0; GetWindowThreadProcessId(target, &current_pid);
        if (current_pid != windows[selection - 1].process_id || !emuscale::capturable_window(target)) throw std::runtime_error("Capture target changed or became unavailable");
        app.capture.start(target, app.preview, app.renderer.device(), app.stats,
                          app.metrics);
        app.diagnostics.event("capture_started", "Confirmed selected Eden HWND and PID; Windows Graphics Capture StartCapture succeeded");
        app.capture_started = std::chrono::steady_clock::now();
        std::wcout << L"[Capture] Windows Graphics Capture session started\n";
        app.status = L"Capturing";
        ShowWindow(app.preview, opt.diagnostics ? SW_SHOWNOACTIVATE : SW_SHOW);
        UpdateWindow(app.preview);
        if (!opt.diagnostics) {
            ShowWindow(app.controls, SW_SHOW);
            SetForegroundWindow(app.controls);
        }
        if (opt.diagnostics) {
            RECT after{}; GetWindowRect(target, &after);
            app.diagnostics.event("startup_window_audit", std::string("target_bounds_unchanged=") +
                (EqualRect(&target_bounds_before, &after) ? "true" : "false") + "; foreground_unchanged=" +
                (foreground_before == GetForegroundWindow() ? "true" : "false") + "; no target window-management calls issued");
        }
        SetTimer(app.preview, 1, 1000, nullptr);

        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    } catch (...) {
        app.diagnostics.event("startup_or_loop_error", "Capture startup or message loop raised an exception");
        app.status = L"Capture error";
        if (IsWindow(app.preview)) DestroyWindow(app.preview);
        throw;
    }
    if (!app.error.empty()) {
        std::wcerr << L"Preview stopped: " << app.error << L"\n";
        return 1;
    }
    return 0;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 2 && std::wstring_view(argv[1]) == L"--help") {
            std::cout << "EmuScale RTX v0.1.2\nNo arguments: v0.1.1 manual selection and controls.\n"
                "--diagnostics [--duration=0|30|60|120] [--scenario=A|B|C]\n"
                "[--ready --optimizer=configuration --scene=label] [--vsync=Auto|On|Off]\n"
                "[--limit=Auto|30|60|90|120|144|165|240|Unlocked] [--target-hwnd=HWND]\n"
                "--list-eden | --display-info | --stop | --mark=1..7\n"
                "Markers: 1 Explore, 2 Combat, 3 Travel, 4 Menu, 5 Loading, 6 Effects, 7 Stutter\n";
            return 0;
        }
        if (argc == 2 && (std::wstring_view(argv[1]) == L"--stop" || std::wstring_view(argv[1]).starts_with(L"--mark="))) {
            HWND preview = FindWindowW(L"EmuScaleRTXPreview", nullptr);
            if (!preview) throw std::runtime_error("No EmuScale preview is running");
            if (FindWindowExW(nullptr, preview, L"EmuScaleRTXPreview", nullptr)) throw std::runtime_error("Multiple EmuScale previews are running; close extras before issuing a command");
            if (std::wstring_view(argv[1]) == L"--stop") {
                if (!PostMessageW(preview, WM_CLOSE, 0, 0)) throw std::runtime_error("Cannot send stop command");
            } else {
                const auto value = std::wstring(argv[1]).substr(7);
                if (value.size() != 1 || value[0] < L'1' || value[0] > L'7') throw std::runtime_error("Marker must be 1..7");
                if (!PostMessageW(preview, marker_message, value[0] - L'1', 0)) throw std::runtime_error("Cannot send marker");
            }
            return 0;
        }
        return run(options(argc, argv));
    }
    catch (const winrt::hresult_error& error) {
        std::wcerr << L"Windows capture error: " << error.message().c_str()
                   << L" (HRESULT 0x" << std::hex << static_cast<unsigned long>(error.code())
                   << L")\n";
    } catch (const std::exception& error) {
        std::cerr << "EmuScale RTX: " << error.what() << '\n';
    }
    return 1;
}
