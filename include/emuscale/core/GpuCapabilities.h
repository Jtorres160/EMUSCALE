#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace emuscale {

enum class Support { unknown, supported, unsupported };
enum class Backend { nis, dlss_super_resolution, custom_optical_flow,
                     custom_compute_interpolation, dlss_frame_generation,
                     dlss_multi_frame_generation };
enum class CompatibilityMode { automatic, conservative };

struct FeatureCapability {
    Backend backend;
    Support hardware = Support::unknown;
    Support sdk = Support::unsupported;
    Support integration = Support::unsupported;
    std::string reason;

    [[nodiscard]] bool available() const noexcept {
        return hardware == Support::supported && sdk == Support::supported &&
               integration == Support::supported;
    }
};

struct GpuInfo {
    std::wstring name;
    std::uint32_t adapter_index = 0;
    std::uint32_t vendor_id = 0;
    std::uint32_t device_id = 0;
    std::uint64_t dedicated_video_memory_bytes = 0;
    std::uint32_t d3d_feature_level = 0;
    bool d3d11_device_created = false;
    std::vector<FeatureCapability> features;

    [[nodiscard]] bool is_nvidia() const noexcept { return vendor_id == 0x10DE; }
};

// Enumerates physical DXGI adapters. Adapter index, not a GPU name, selects a device.
[[nodiscard]] std::vector<GpuInfo> detect_gpus();

// The policy layer is deterministic and can be tested without a GPU.
[[nodiscard]] std::vector<FeatureCapability> make_capability_matrix(const GpuInfo& gpu);
[[nodiscard]] bool can_select(const FeatureCapability& feature,
                              CompatibilityMode mode) noexcept;
// Returns nullptr to retain the v0.1 unprocessed capture path.
[[nodiscard]] const FeatureCapability* select_upscaler(
    const std::vector<FeatureCapability>& features,
    CompatibilityMode mode, Backend requested) noexcept;
[[nodiscard]] const FeatureCapability* select_frame_generator(
    const std::vector<FeatureCapability>& features,
    CompatibilityMode mode, Backend requested) noexcept;
[[nodiscard]] const char* backend_name(Backend backend) noexcept;
[[nodiscard]] const char* support_name(Support support) noexcept;

} // namespace emuscale
