#include "emuscale/core/GpuCapabilities.h"

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <utility>

namespace emuscale {

std::vector<GpuInfo> detect_gpus() {
    std::vector<GpuInfo> result;
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return result;

    for (UINT index = 0;; ++index) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        const HRESULT enumeration = factory->EnumAdapters1(index, &adapter);
        if (enumeration == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(enumeration)) break;

        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(adapter->GetDesc1(&description))) continue;
        if (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;

        GpuInfo gpu;
        gpu.name = description.Description;
        gpu.adapter_index = index;
        gpu.vendor_id = description.VendorId;
        gpu.device_id = description.DeviceId;
        gpu.dedicated_video_memory_bytes = description.DedicatedVideoMemory;

        Microsoft::WRL::ComPtr<ID3D11Device> device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        D3D_FEATURE_LEVEL level{};
        constexpr D3D_FEATURE_LEVEL requested[] = {
            D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
        };
        HRESULT hr = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN,
            nullptr, 0, requested, _countof(requested), D3D11_SDK_VERSION,
            &device, &level, &context);
        if (hr == E_INVALIDARG) { // Older Windows runtimes do not accept 11_1.
            hr = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN,
                nullptr, 0, requested + 1, _countof(requested) - 1,
                D3D11_SDK_VERSION, &device, &level, &context);
        }
        gpu.d3d11_device_created = SUCCEEDED(hr);
        if (gpu.d3d11_device_created) gpu.d3d_feature_level = level;
        gpu.features = make_capability_matrix(gpu);
        result.push_back(std::move(gpu));
    }
    return result;
}

} // namespace emuscale
