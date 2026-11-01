#include "emuscale/capture/WindowCapture.h"

#include <dxgi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.capture.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Foundation.h>
#include <wrl/client.h>
#include <stdexcept>

namespace emuscale {
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;

WindowCapture::~WindowCapture() { stop(); }

void WindowCapture::start(HWND target, HWND notify, ID3D11Device* device,
                          std::shared_ptr<CaptureStats> stats) {
    stop();
    if (!IsWindow(target) || !device || !notify) throw std::invalid_argument("Invalid capture target or device");
    if (!GraphicsCaptureSession::IsSupported()) throw std::runtime_error("Windows Graphics Capture is unavailable");

    auto shared = std::make_shared<Shared>();
    shared->notify = notify;
    shared->stats = std::move(stats);

    auto factory = winrt::get_activation_factory<GraphicsCaptureItem>();
    auto interop = factory.as<IGraphicsCaptureItemInterop>();
    GraphicsCaptureItem item{nullptr};
    winrt::check_hresult(interop->CreateForWindow(target,
        winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
        reinterpret_cast<void**>(winrt::put_abi(item))));

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
    winrt::check_hresult(device->QueryInterface(IID_PPV_ARGS(&dxgi)));
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice rt_device{nullptr};
    winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(),
        reinterpret_cast<IInspectable**>(winrt::put_abi(rt_device))));

    const auto size = item.Size();
    if (size.Width <= 0 || size.Height <= 0) throw std::runtime_error("Capture target has no drawable size");
    auto pool = Direct3D11CaptureFramePool::CreateFreeThreaded(
        rt_device, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
    auto session = pool.CreateCaptureSession(item);

    std::weak_ptr<Shared> weak = shared;
    const auto frame_token = pool.FrameArrived([weak](auto const& sender, auto const&) {
        auto state = weak.lock();
        if (!state) return;
        Shared::CallbackGuard guard(*state);
        if (state->paused.load(std::memory_order_acquire)) return;
        try {
            auto frame = sender.TryGetNextFrame();
            if (!frame) return;
            const auto size = frame.ContentSize();
            state->stats->captured.fetch_add(1, std::memory_order_relaxed);
            if (size.Width > 0 && size.Height > 0) {
                state->stats->capture_width.store(size.Width, std::memory_order_relaxed);
                state->stats->capture_height.store(size.Height, std::memory_order_relaxed);
            }
            {
                std::lock_guard lock(state->mutex);
                if (state->latest) {
                    state->latest->Close();
                    state->stats->overwritten.fetch_add(1, std::memory_order_relaxed);
                }
                state->latest = std::move(frame);
            }
            PostMessageW(state->notify, capture_frame_message, 0, 0);
        } catch (const winrt::hresult_error& error) {
            state->stats->capture_hresult.store(error.code(), std::memory_order_relaxed);
            state->stats->capture_failed.store(true, std::memory_order_relaxed);
            PostMessageW(state->notify, capture_failed_message, 0, 0);
        } catch (...) {
            state->stats->capture_hresult.store(E_FAIL, std::memory_order_relaxed);
            state->stats->capture_failed.store(true, std::memory_order_relaxed);
            PostMessageW(state->notify, capture_failed_message, 0, 0);
        }
    });
    const auto closed_token = item.Closed([weak](auto const&, auto const&) {
        if (auto state = weak.lock()) {
            Shared::CallbackGuard guard(*state);
            state->stats->target_closed.store(true, std::memory_order_relaxed);
            if (!state->paused.load(std::memory_order_acquire))
                PostMessageW(state->notify, capture_closed_message, 0, 0);
        }
    });

    shared_ = std::move(shared);
    item_ = std::move(item);
    pool_ = std::move(pool);
    session_ = std::move(session);
    winrt_device_ = std::move(rt_device);
    frame_token_ = frame_token;
    closed_token_ = closed_token;
    try { session_.StartCapture(); }
    catch (...) { stop(); throw; }
}

void WindowCapture::stop() noexcept {
    if (shared_) shared_->paused.store(true, std::memory_order_release);
    try { if (pool_) pool_.FrameArrived(frame_token_); } catch (...) {}
    try { if (item_) item_.Closed(closed_token_); } catch (...) {}
    if (shared_) {
        std::unique_lock lock(shared_->mutex);
        shared_->callbacks_done.wait(lock, [&] {
            return shared_->active_callbacks.load(std::memory_order_acquire) == 0;
        });
        try { if (shared_->latest) shared_->latest->Close(); } catch (...) {}
        shared_->latest.reset();
    }
    try { if (session_) session_.Close(); } catch (...) {}
    try { if (pool_) pool_.Close(); } catch (...) {}
    session_ = nullptr;
    pool_ = nullptr;
    item_ = nullptr;
    winrt_device_ = nullptr;
    shared_.reset();
}

std::optional<WindowCapture::Frame> WindowCapture::take_frame() {
    if (!shared_) return std::nullopt;
    std::lock_guard lock(shared_->mutex);
    auto frame = std::move(shared_->latest);
    shared_->latest.reset();
    return frame;
}

void WindowCapture::resize_pool(winrt::Windows::Graphics::SizeInt32 size) {
    if (!pool_ || size.Width <= 0 || size.Height <= 0) return;
    if (shared_) {
        shared_->paused.store(true, std::memory_order_release);
        std::unique_lock lock(shared_->mutex);
        shared_->callbacks_done.wait(lock, [&] {
            return shared_->active_callbacks.load(std::memory_order_acquire) == 0;
        });
        if (shared_->latest) shared_->latest->Close();
        shared_->latest.reset();
    }
    try {
        pool_.Recreate(winrt_device_, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
    } catch (...) {
        if (shared_) shared_->paused.store(false, std::memory_order_release);
        throw;
    }
    if (shared_) shared_->paused.store(false, std::memory_order_release);
}
} // namespace emuscale
