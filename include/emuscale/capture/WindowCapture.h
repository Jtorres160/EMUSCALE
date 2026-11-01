#pragma once
#include "emuscale/core/CaptureStats.h"
#include <d3d11.h>
#include <windows.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <memory>
#include <mutex>
#include <optional>
#include <atomic>
#include <condition_variable>

namespace emuscale {
constexpr UINT capture_frame_message = WM_APP + 1;
constexpr UINT capture_closed_message = WM_APP + 2;
constexpr UINT capture_failed_message = WM_APP + 3;

class WindowCapture {
public:
    WindowCapture() = default;
    ~WindowCapture();
    WindowCapture(const WindowCapture&) = delete;
    WindowCapture& operator=(const WindowCapture&) = delete;

    void start(HWND target, HWND notify, ID3D11Device* device,
               std::shared_ptr<CaptureStats> stats);
    void stop() noexcept;
    [[nodiscard]] std::optional<winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame> take_frame();
    void resize_pool(winrt::Windows::Graphics::SizeInt32 size);

private:
    using Frame = winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame;
    struct Shared {
        std::mutex mutex;
        std::optional<Frame> latest;
        HWND notify = nullptr;
        std::shared_ptr<CaptureStats> stats;
        std::atomic<bool> paused{false};
        std::atomic<unsigned> active_callbacks{0};
        std::condition_variable callbacks_done;
        struct CallbackGuard {
            Shared& state;
            explicit CallbackGuard(Shared& value) : state(value) {
                state.active_callbacks.fetch_add(1, std::memory_order_acq_rel);
            }
            ~CallbackGuard() {
                if (state.active_callbacks.fetch_sub(1, std::memory_order_acq_rel) == 1)
                    state.callbacks_done.notify_all();
            }
        };
    };
    std::shared_ptr<Shared> shared_;
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem item_{nullptr};
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool pool_{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession session_{nullptr};
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice winrt_device_{nullptr};
    winrt::event_token frame_token_{};
    winrt::event_token closed_token_{};
};
} // namespace emuscale
