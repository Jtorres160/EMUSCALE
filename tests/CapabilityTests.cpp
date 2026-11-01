#include "emuscale/core/GpuCapabilities.h"

#include <cassert>

int main() {
    emuscale::GpuInfo gpu;
    gpu.name = L"GeForce GTX 1080 Ti";
    gpu.vendor_id = 0x10DE;
    gpu.d3d11_device_created = true;
    gpu.d3d_feature_level = 0xB000;
    const auto features = emuscale::make_capability_matrix(gpu);
    assert(features.size() == 6);
    assert(features[0].hardware == emuscale::Support::supported);
    for (const auto& feature : features) {
        assert(!feature.available());
        assert(!emuscale::can_select(feature, emuscale::CompatibilityMode::automatic));
        assert(!emuscale::can_select(feature, emuscale::CompatibilityMode::conservative));
    }
    assert(emuscale::select_upscaler(features, emuscale::CompatibilityMode::automatic,
                                    emuscale::Backend::dlss_super_resolution) == nullptr);
    assert(emuscale::select_frame_generator(features, emuscale::CompatibilityMode::automatic,
                                           emuscale::Backend::custom_optical_flow) == nullptr);
    auto ready = features;
    ready[0].sdk = ready[0].integration = emuscale::Support::supported;
    ready[1].hardware = ready[1].sdk = ready[1].integration = emuscale::Support::supported;
    assert(emuscale::select_upscaler(ready, emuscale::CompatibilityMode::automatic,
                                    emuscale::Backend::dlss_super_resolution)->backend ==
           emuscale::Backend::dlss_super_resolution);
    assert(emuscale::select_upscaler(ready, emuscale::CompatibilityMode::conservative,
                                    emuscale::Backend::dlss_super_resolution)->backend ==
           emuscale::Backend::nis);
    gpu.vendor_id = 0x8086;
    const auto other = emuscale::make_capability_matrix(gpu);
    assert(other[1].hardware == emuscale::Support::unsupported);
    assert(other[2].hardware == emuscale::Support::unsupported);
}
