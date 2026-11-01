#include "emuscale/graphics/ProcessingPipeline.h"
#include <chrono>
#include <stdexcept>

namespace emuscale {
void ProcessingPipeline::initialize(ID3D11Device* device, ID3D11DeviceContext* context) {
    reset();
    if (!device || !context || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) throw std::invalid_argument("Processing requires an immediate D3D11 context");
    Microsoft::WRL::ComPtr<ID3D11Device> owner; context->GetDevice(&owner);
    if (owner.Get() != device) throw std::invalid_argument("Processing device/context mismatch");
    device_ = device; context_ = context;
}
void ProcessingPipeline::set_processor(std::unique_ptr<IGpuProcessor> processor) {
    if (processor_) processor_->reset();
    processor_ = std::move(processor);
    gpu_ms_ = -1; completed_ = 0; sequence_ = 0; timings_ = {};
    timing_available_ = processor_ != nullptr && device_ != nullptr;
    if (!timing_available_) return;
    for (auto& slot : timings_) {
        D3D11_QUERY_DESC desc{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
        if (FAILED(device_->CreateQuery(&desc, &slot.disjoint))) { timing_available_ = false; break; }
        desc.Query = D3D11_QUERY_TIMESTAMP;
        if (FAILED(device_->CreateQuery(&desc, &slot.begin)) || FAILED(device_->CreateQuery(&desc, &slot.end))) { timing_available_ = false; break; }
    }
}
bool ProcessingPipeline::compatible(const GpuFrame& frame) const {
    if (!device_ || !frame.texture || !frame.content.width || !frame.content.height) return false;
    Microsoft::WRL::ComPtr<ID3D11Device> owner; frame.texture->GetDevice(&owner);
    D3D11_TEXTURE2D_DESC d{}; frame.texture->GetDesc(&d);
    return owner.Get() == device_.Get() && frame.content.width <= d.Width && frame.content.height <= d.Height &&
        d.SampleDesc.Count == 1 && d.ArraySize == 1 && d.MipLevels == 1 && d.Usage == D3D11_USAGE_DEFAULT && !d.CPUAccessFlags &&
        (d.Format == DXGI_FORMAT_B8G8R8A8_UNORM || d.Format == DXGI_FORMAT_R8G8B8A8_UNORM);
}
void ProcessingPipeline::collect_timing() {
    if (!timing_available_) return;
    for (auto& slot : timings_) if (slot.pending) {
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT info{};
        UINT64 begin = 0, end = 0;
        const auto a = context_->GetData(slot.disjoint.Get(), &info, sizeof(info), D3D11_ASYNC_GETDATA_DONOTFLUSH);
        const auto b = context_->GetData(slot.begin.Get(), &begin, sizeof(begin), D3D11_ASYNC_GETDATA_DONOTFLUSH);
        const auto c = context_->GetData(slot.end.Get(), &end, sizeof(end), D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (FAILED(a) || FAILED(b) || FAILED(c)) { timing_available_ = false; gpu_ms_ = -1; break; }
        if (a == S_OK && b == S_OK && c == S_OK) {
            slot.pending = false;
            if (slot.sequence > completed_) {
                completed_ = slot.sequence;
                gpu_ms_ = !info.Disjoint && info.Frequency && end >= begin ? 1000.0 * (end - begin) / info.Frequency : -1;
            }
        }
    }
}
ProcessingResult ProcessingPipeline::process(const GpuFrame& source, Extent requested) {
    if (!compatible(source) || !requested.width || !requested.height) throw std::invalid_argument("Invalid GPU processing input or output dimensions");
    const auto begin = std::chrono::steady_clock::now();
    ProcessingResult result; result.output = source;
    collect_timing();
    TimingSlot* timing = nullptr;
    if (processor_ && timing_available_) for (auto& slot : timings_) if (!slot.pending) { timing = &slot; break; }
    if (timing) { context_->Begin(timing->disjoint.Get()); context_->End(timing->begin.Get()); }
    if (processor_) {
        try {
            auto output = processor_->process(device_.Get(), context_.Get(), source, requested);
            if (!compatible(output) || output.content.width != requested.width || output.content.height != requested.height ||
                output.source_timestamp_100ns != source.source_timestamp_100ns) throw std::runtime_error("Processor returned incompatible texture, dimensions or source timestamp");
            result.output = std::move(output);
        } catch (const std::exception& e) { result.fallback = true; result.reason = e.what(); }
        catch (...) { result.fallback = true; result.reason = "Unknown processor failure"; }
    }
    if (timing) {
        context_->End(timing->end.Get()); context_->End(timing->disjoint.Get());
        timing->pending = true; timing->sequence = ++sequence_;
    }
    if (result.fallback) { processor_->reset(); processor_.reset(); context_->ClearState(); }
    result.last_completed_gpu_ms = gpu_ms_; result.gpu_timing_sequence = completed_;
    result.submit_cpu_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    return result;
}
void ProcessingPipeline::reset() noexcept {
    if (processor_) processor_->reset();
    processor_.reset(); timings_ = {}; context_.Reset(); device_.Reset();
    timing_available_ = false; sequence_ = completed_ = 0; gpu_ms_ = -1;
}
}
