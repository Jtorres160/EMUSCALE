#include "emuscale/core/GpuCapabilities.h"

#include <iostream>
#include <string_view>

int wmain(int argc, wchar_t** argv) {
    emuscale::CompatibilityMode mode = emuscale::CompatibilityMode::automatic;
    if (argc == 2 && std::wstring_view(argv[1]) == L"--compatibility=conservative") {
        mode = emuscale::CompatibilityMode::conservative;
    } else if (argc == 2 && std::wstring_view(argv[1]) == L"--compatibility=automatic") {
        mode = emuscale::CompatibilityMode::automatic;
    } else if (argc != 1) {
        std::wcerr << L"Usage: emuscale_gpu_info [--compatibility=automatic|conservative]\n";
        return 2;
    }

    const auto gpus = emuscale::detect_gpus();
    if (gpus.empty()) {
        std::wcerr << L"No physical DXGI adapters found or DXGI initialization failed.\n";
        return 1;
    }
    for (const auto& gpu : gpus) {
        std::wcout << L"Adapter " << gpu.adapter_index << L": " << gpu.name
                   << L" (vendor 0x" << std::hex << gpu.vendor_id
                   << L", device 0x" << gpu.device_id << std::dec << L")\n";
        std::wcout << L"  D3D11 device: " << (gpu.d3d11_device_created ? L"yes" : L"no")
                   << L", feature level: 0x" << std::hex << gpu.d3d_feature_level
                   << std::dec << L"\n";
        for (const auto& feature : gpu.features) {
            std::wcout << L"  " << emuscale::backend_name(feature.backend)
                       << L": hardware=" << emuscale::support_name(feature.hardware)
                       << L", sdk=" << emuscale::support_name(feature.sdk)
                       << L", integration=" << emuscale::support_name(feature.integration)
                       << L", selectable=" << (emuscale::can_select(feature, mode) ? L"yes" : L"no")
                       << L"\n";
        }
    }
    return 0;
}
