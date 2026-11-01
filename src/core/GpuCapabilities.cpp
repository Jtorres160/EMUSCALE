#include "emuscale/core/GpuCapabilities.h"

namespace emuscale {

std::vector<FeatureCapability> make_capability_matrix(const GpuInfo& gpu) {
    const bool nvidia = gpu.vendor_id == 0x10DE;
    const bool d3d11_compute = gpu.d3d11_device_created &&
                               gpu.d3d_feature_level >= 0xB000; // D3D_FEATURE_LEVEL_11_0
    return {
        {Backend::nis, d3d11_compute ? Support::supported :
             (nvidia ? Support::unknown : Support::unsupported),
             Support::unsupported, Support::unsupported,
             "NIS SDK shader has not been integrated; D3D11 feature level is only a prerequisite"},
        {Backend::dlss_super_resolution, nvidia ? Support::unknown : Support::unsupported,
             Support::unsupported, Support::unsupported,
             "Requires RTX eligibility, SDK query, and native renderer inputs"},
        {Backend::custom_optical_flow, nvidia ? Support::unknown : Support::unsupported,
             Support::unsupported, Support::unsupported,
             "NVIDIA Optical Flow SDK support must be queried on the selected adapter"},
        {Backend::custom_compute_interpolation, d3d11_compute ? Support::supported : Support::unknown,
             Support::unsupported, Support::unsupported,
             "Compute feasibility is unproven; no interpolation implementation exists"},
        {Backend::dlss_frame_generation, nvidia ? Support::unknown : Support::unsupported,
             Support::unsupported, Support::unsupported,
             "Requires RTX 40+ eligibility and native renderer integration"},
        {Backend::dlss_multi_frame_generation, nvidia ? Support::unknown : Support::unsupported,
             Support::unsupported, Support::unsupported,
             "Requires RTX 50+ eligibility and runtime Streamline state query"}
    };
}

bool can_select(const FeatureCapability& feature, CompatibilityMode mode) noexcept {
    if (!feature.available()) return false;
    return mode != CompatibilityMode::conservative || feature.backend == Backend::nis;
}

const FeatureCapability* select_upscaler(
    const std::vector<FeatureCapability>& features,
    CompatibilityMode mode, Backend requested) noexcept {
    if (requested == Backend::nis || requested == Backend::dlss_super_resolution) {
        for (const auto& feature : features)
            if (feature.backend == requested && can_select(feature, mode)) return &feature;
    }
    for (const auto& feature : features)
        if (feature.backend == Backend::nis && can_select(feature, mode)) return &feature;
    return nullptr;
}

const FeatureCapability* select_frame_generator(
    const std::vector<FeatureCapability>& features,
    CompatibilityMode mode, Backend requested) noexcept {
    if (requested != Backend::custom_optical_flow &&
        requested != Backend::custom_compute_interpolation &&
        requested != Backend::dlss_frame_generation &&
        requested != Backend::dlss_multi_frame_generation) return nullptr;
    for (const auto& feature : features)
        if (feature.backend == requested && can_select(feature, mode)) return &feature;
    return nullptr; // No silent substitution between native and custom generation.
}

const char* backend_name(Backend backend) noexcept {
    switch (backend) {
    case Backend::nis: return "NVIDIA Image Scaling";
    case Backend::dlss_super_resolution: return "DLSS Super Resolution";
    case Backend::custom_optical_flow: return "Custom optical-flow interpolation";
    case Backend::custom_compute_interpolation: return "Custom compute interpolation";
    case Backend::dlss_frame_generation: return "DLSS Frame Generation";
    case Backend::dlss_multi_frame_generation: return "DLSS Multi Frame Generation";
    }
    return "Unknown";
}

const char* support_name(Support support) noexcept {
    switch (support) {
    case Support::unknown: return "unknown";
    case Support::supported: return "supported";
    case Support::unsupported: return "unsupported";
    }
    return "unknown";
}

} // namespace emuscale
