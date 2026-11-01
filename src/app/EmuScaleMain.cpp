#include "emuscale/capture/WindowCapture.h"
#include "emuscale/core/CaptureStats.h"
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
#include <stdexcept>
#include <string>

namespace {
struct AppState {
    HWND target = nullptr;
    HWND preview = nullptr;
    emuscale::PreviewRenderer renderer;
    emuscale::WindowCapture capture;
    std::shared_ptr<emuscale::CaptureStats> stats = std::make_shared<emuscale::CaptureStats>();
    std::uint64_t previous_captured = 0;
    std::uint64_t previous_presented = 0;
    std::wstring status = L"Starting";
    std::wstring error;
};

std::wstring hresult_text(long code) {
    std::wostringstream text;
    text << L"HRESULT 0x" << std::hex << static_cast<unsigned long>(code);
    return text.str();
}

void show_frame(AppState& app) {
    auto frame = app.capture.take_frame();
    if (!frame) return;
    const auto size = frame->ContentSize();
    if (size.Width <= 0 || size.Height <= 0) return;
    auto access = frame->Surface().as<
        ::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(&texture)));
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    app.renderer.present(texture.Get(), static_cast<UINT>(size.Width),
                         static_cast<UINT>(size.Height));
    app.stats->presented.fetch_add(1, std::memory_order_relaxed);
    texture.Reset();
    frame->Close();
    frame.reset();
    if (static_cast<UINT>(size.Width) != desc.Width ||
        static_cast<UINT>(size.Height) != desc.Height) {
        app.capture.resize_pool(size);
    }
}

void update_title(AppState& app) {
    const auto captured = app.stats->captured.load(std::memory_order_relaxed);
    const auto presented = app.stats->presented.load(std::memory_order_relaxed);
    const auto dropped = app.stats->overwritten.load(std::memory_order_relaxed);
    const auto width = app.stats->capture_width.load(std::memory_order_relaxed);
    const auto height = app.stats->capture_height.load(std::memory_order_relaxed);
    std::wstring title = L"EmuScale RTX v0.1 | " + app.status + L" | " +
        app.renderer.gpu_name() + L" | capture " +
        std::to_wstring(captured - app.previous_captured) + L" FPS | preview " +
        std::to_wstring(presented - app.previous_presented) + L" FPS | skipped " +
        std::to_wstring(dropped) + L" | " + std::to_wstring(width) + L"x" +
        std::to_wstring(height) + L" -> " + std::to_wstring(app.renderer.width()) +
        L"x" + std::to_wstring(app.renderer.height());
    SetWindowTextW(app.preview, title.c_str());
    std::wcout << L"[Capture] " << app.renderer.gpu_name() << L" | "
               << (captured - app.previous_captured) << L" captured FPS | "
               << (presented - app.previous_presented) << L" presented FPS | "
               << width << L"x" << height << L" -> "
               << app.renderer.width() << L"x" << app.renderer.height()
               << L" | overwritten " << dropped << L'\n';
    app.previous_captured = captured;
    app.previous_presented = presented;
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
            app->capture.stop();
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
    std::wcout << L"Select a window to capture (emulators listed first):\n";
    for (std::size_t i = 0; i < windows.size(); ++i) {
        const auto& item = windows[i];
        std::wcout << i + 1 << L". " << item.title << L" | " << item.process_name
                   << L" | PID " << item.process_id
                   << (item.minimized ? L" | minimized (restore first)" : L"") << L"\n";
    }
    std::wcout << L"Number: " << std::flush;
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
    app.preview = CreateWindowExW(0, window_class.lpszClassName, L"EmuScale RTX v0.1",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 760,
        nullptr, nullptr, instance, &app);
    if (!app.preview) throw std::runtime_error("CreateWindowEx failed");
    try {
        app.renderer.initialize(app.preview);
        std::wcout << L"[Preview] D3D11 swap chain initialized on "
                   << app.renderer.gpu_name() << L'\n';
        app.capture.start(target, app.preview, app.renderer.device(), app.stats);
        std::wcout << L"[Capture] Windows Graphics Capture session started\n";
        app.status = L"Capturing";
        ShowWindow(app.preview, SW_SHOW);
        UpdateWindow(app.preview);
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
