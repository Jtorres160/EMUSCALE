#include "emuscale/capture/WindowCapture.h"
#include "emuscale/core/CaptureStats.h"
#include "emuscale/core/Benchmark.h"
#include "emuscale/core/FramePacing.h"
#include "emuscale/core/FrameMetrics.h"
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

namespace {
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
    app.renderer.present(texture.Get(), static_cast<UINT>(size.Width),
                         static_cast<UINT>(size.Height));
    app.stats->presented.fetch_add(1, std::memory_order_relaxed);
    const auto [presented_at, present_frequency] = qpc();
    app.metrics->on_present(presented_at, present_frequency);
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
    const auto [now, frequency] = qpc();
    const auto measured = app.metrics->snapshot(now, frequency);
    const auto dropped = app.stats->overwritten.load(std::memory_order_relaxed);
    const auto width = app.stats->capture_width.load(std::memory_order_relaxed);
    const auto height = app.stats->capture_height.load(std::memory_order_relaxed);
    const int cap = emuscale::fps_cap(app.pacer.limit());
    const int target = cap ? cap : app.pacer.limit() == emuscale::FpsLimit::automatic ?
        app.monitor_refresh_hz : 0;
    std::wstring title = L"EmuScale RTX v0.1.1 | " + app.status + L" | " +
        app.renderer.gpu_name() + L" | source timestamps " +
        std::to_wstring(static_cast<int>(measured.distinct_capture_fps)) + L" FPS | present " +
        std::to_wstring(static_cast<int>(measured.presented_fps)) + L" FPS | target " +
        (target ? std::to_wstring(target) : L"Unlocked") + L" | " +
        std::to_wstring(width) + L"x" + std::to_wstring(height) + L" -> " +
        std::to_wstring(app.renderer.width()) + L"x" +
        std::to_wstring(app.renderer.height());
    SetWindowTextW(app.preview, title.c_str());
    std::wcout << L"[Capture] " << app.renderer.gpu_name() << L" | source timestamps "
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
              << L"Diagnostic: " << diagnose(measured, cap, target, dropped);
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
        WS_MINIMIZEBOX, 80, 80, 650, 405, nullptr,
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
    SendMessageW(fps, CB_SETCURSEL, 0, 0);
    label(L"VSync", 220, 16, 120);
    HWND vsync = combo(vsync_combo_id, 220, 40, 150);
    for (int i = 0; i <= 2; ++i) SendMessageW(vsync, CB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(emuscale::vsync_name(static_cast<emuscale::VSyncMode>(i))));
    SendMessageW(vsync, CB_SETCURSEL, 0, 0);
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
        WS_CHILD | WS_VISIBLE | SS_LEFT, 16, 151, 610, 195,
        app.controls, nullptr, instance, nullptr);
    label(L"Distinct timestamps are not proof of distinct game images. F1 reopens this panel.",
          16, 348, 620);
    ShowWindow(app.controls, SW_SHOW);
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
            return 0;
        case WM_KEYDOWN:
            if (wparam == VK_F1 && app->controls) {
                ShowWindow(app->controls, SW_SHOW);
                SetForegroundWindow(app->controls);
            }
            return 0;
        case WM_TIMER:
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

int run() {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    auto windows = emuscale::discover_windows();
    if (windows.empty()) {
        std::wcout << L"No visible application windows found. Start an emulator and retry.\n";
        return 1;
    }
    write_console(L"Select a window to capture (emulators listed first):\r\n");
    for (std::size_t i = 0; i < windows.size(); ++i) {
        const auto& item = windows[i];
        std::wstring line = std::to_wstring(i + 1) + L". " + item.title + L" | " +
            item.process_name + L" | PID " + std::to_wstring(item.process_id) +
            (item.minimized ? L" | minimized (restore first)" : L"") + L"\r\n";
        write_console(line);
    }
    write_console(L"Number: ");
    std::size_t selection = 0;
    if (!(std::wcin >> selection) || selection < 1 || selection > windows.size())
        throw std::runtime_error("Invalid selection");
    const HWND target = windows[selection - 1].handle;
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
    app.preview = CreateWindowExW(0, window_class.lpszClassName, L"EmuScale RTX v0.1.1",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 760,
        nullptr, nullptr, instance, &app);
    if (!app.preview) throw std::runtime_error("CreateWindowEx failed");
    try {
        app.renderer.initialize(app.preview);
        app.monitor_refresh_hz = refresh_rate(app.preview);
        std::wcout << L"[Preview] D3D11 swap chain initialized on "
                   << app.renderer.gpu_name() << L" | tearing "
                   << (app.renderer.tearing_supported() ? L"supported" : L"unavailable") << L'\n';
        create_controls(app, instance);
        app.capture.start(target, app.preview, app.renderer.device(), app.stats,
                          app.metrics);
        std::wcout << L"[Capture] Windows Graphics Capture session started\n";
        app.status = L"Capturing";
        ShowWindow(app.preview, SW_SHOW);
        UpdateWindow(app.preview);
        ShowWindow(app.controls, SW_SHOW);
        SetForegroundWindow(app.controls);
        SetTimer(app.preview, 1, 1000, nullptr);

        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    } catch (...) {
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

int wmain() {
    try { return run(); }
    catch (const winrt::hresult_error& error) {
        std::wcerr << L"Windows capture error: " << error.message().c_str()
                   << L" (HRESULT 0x" << std::hex << static_cast<unsigned long>(error.code())
                   << L")\n";
    } catch (const std::exception& error) {
        std::cerr << "EmuScale RTX: " << error.what() << '\n';
    }
    return 1;
}
