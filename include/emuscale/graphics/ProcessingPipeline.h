#pragma once
#include "emuscale/windows/DisplayPolicy.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace emuscale {
// Owns a COM reference, not the WGC frame lease. Release before closing that lease.
struct GpuFrame {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Extent content;
    std::int64_t source_timestamp_100ns = 0;
};
class IGpuProcessor {
public:
    virtual ~IGpuProcessor() = default;
    // Input is read-only. Unbind output UAVs/SRVs before returning; reset must
    // release owned resources. Do not retain a captured source beyond this call.
    virtual GpuFrame process(ID3D11Device* device, ID3D11DeviceContext* context,
                             const GpuFrame& source, Extent requested_output) = 0;
    virtual void reset() noexcept = 0;
};
struct ProcessingResult {
    GpuFrame output;
    bool fallback = false;
    std::string reason;
    double submit_cpu_ms = 0, last_completed_gpu_ms = -1;
    std::uint64_t gpu_timing_sequence = 0;
};
class ProcessingPipeline {
public:
    ~ProcessingPipeline() { reset(); }
    void initialize(ID3D11Device* device, ID3D11DeviceContext* context);
    void set_processor(std::unique_ptr<IGpuProcessor> processor);
    ProcessingResult process(const GpuFrame& source, Extent requested_output);
    void reset() noexcept;
    bool enabled() const noexcept { return processor_ != nullptr; }
private:
    bool compatible(const GpuFrame& frame) const;
    struct TimingSlot {
        Microsoft::WRL::ComPtr<ID3D11Query> disjoint, begin, end;
        bool pending = false;
        std::uint64_t sequence = 0;
    };
    void collect_timing();
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    std::unique_ptr<IGpuProcessor> processor_;
    std::array<TimingSlot, 4> timings_;
    bool timing_available_ = false;
    std::uint64_t sequence_ = 0, completed_ = 0;
    double gpu_ms_ = -1;
};
}
