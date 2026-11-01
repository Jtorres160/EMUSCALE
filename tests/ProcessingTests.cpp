#include "emuscale/graphics/ProcessingPipeline.h"
#include <cassert>
#include <chrono>
#include <thread>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
using namespace emuscale;
struct Counts { int calls = 0, resets = 0; };
class Processor : public IGpuProcessor {
public:
    Processor(Counts& counts, bool fail) : counts_(counts), fail_(fail) {}
    GpuFrame process(ID3D11Device* device, ID3D11DeviceContext* context, const GpuFrame& source, Extent output) override {
        ++counts_.calls;
        if (fail_) throw std::runtime_error("synthetic processing failure");
        D3D11_TEXTURE2D_DESC d{}; source.texture->GetDesc(&d);
        ComPtr<ID3D11Texture2D> texture; assert(SUCCEEDED(device->CreateTexture2D(&d, nullptr, &texture)));
        context->CopyResource(texture.Get(), source.texture.Get()); // test-only optional GPU processor
        return {texture, output, source.source_timestamp_100ns};
    }
    void reset() noexcept override { ++counts_.resets; }
private: Counts& counts_; bool fail_;
};
class WrongTimestamp : public IGpuProcessor {
public:
    explicit WrongTimestamp(int& resets) : resets_(resets) {}
    GpuFrame process(ID3D11Device*, ID3D11DeviceContext*, const GpuFrame& source, Extent) override {
        auto output = source; ++output.source_timestamp_100ns; return output;
    }
    void reset() noexcept override { ++resets_; }
private: int& resets_;
};
int main() {
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL level{};
    assert(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr, 0, D3D11_SDK_VERSION, &device, &level, &context)));
    D3D11_TEXTURE2D_DESC d{}; d.Width = 64; d.Height = 36; d.MipLevels = d.ArraySize = 1;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM; d.SampleDesc.Count = 1; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> texture; assert(SUCCEEDED(device->CreateTexture2D(&d, nullptr, &texture)));
    Counts counts;
    {
        ProcessingPipeline pipeline; pipeline.initialize(device.Get(), context.Get());
        GpuFrame input{texture, {64, 36}, 1234};
        auto pass = pipeline.process(input, {1920, 1080});
        assert(pass.output.texture.Get() == texture.Get() && pass.output.content.width == 64);
        assert(pass.output.source_timestamp_100ns == 1234 && !pass.fallback && pass.last_completed_gpu_ms == -1);
        auto rgba_desc = d; rgba_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        ComPtr<ID3D11Texture2D> rgba; assert(SUCCEEDED(device->CreateTexture2D(&rgba_desc, nullptr, &rgba)));
        assert(pipeline.process({rgba, {64, 36}, 1234}, {64, 36}).output.texture.Get() == rgba.Get());
        auto hdr_desc = d; hdr_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        ComPtr<ID3D11Texture2D> hdr; assert(SUCCEEDED(device->CreateTexture2D(&hdr_desc, nullptr, &hdr)));
        bool hdr_rejected = false; try { pipeline.process({hdr, {64, 36}, 0}, {64, 36}); } catch (...) { hdr_rejected = true; }
        assert(hdr_rejected); // Current path is explicitly SDR, no silent format conversion.
        pipeline.set_processor(std::make_unique<Processor>(counts, true));
        auto fallback = pipeline.process(input, {64, 36});
        assert(fallback.fallback && fallback.output.texture.Get() == texture.Get() && !pipeline.enabled());
        pipeline.process(input, {64, 36}); assert(counts.calls == 1 && counts.resets == 1);
        pipeline.set_processor(std::make_unique<Processor>(counts, false));
        auto processed = pipeline.process(input, {64, 36});
        assert(processed.output.texture.Get() != texture.Get() && processed.output.source_timestamp_100ns == 1234);
        context->Flush(); // test only; production GetData never flushes or spins.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        do {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            processed = pipeline.process(input, {64, 36});
        } while (!processed.gpu_timing_sequence && std::chrono::steady_clock::now() < deadline);
        assert(processed.gpu_timing_sequence > 0 && processed.last_completed_gpu_ms >= 0);
        bool invalid = false; try { pipeline.process({texture, {65, 36}, 0}, {64, 36}); } catch (...) { invalid = true; }
        assert(invalid);
        ComPtr<ID3D11Device> other; ComPtr<ID3D11DeviceContext> other_context;
        assert(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &other, &level, &other_context)));
        ComPtr<ID3D11Texture2D> foreign; assert(SUCCEEDED(other->CreateTexture2D(&d, nullptr, &foreign)));
        invalid = false; try { pipeline.process({foreign, {64, 36}, 0}, {64, 36}); } catch (...) { invalid = true; } assert(invalid);
    }
    assert(counts.resets == 2); // active processor reset during pipeline destruction
    int invalid_resets = 0;
    {
        ProcessingPipeline pipeline; pipeline.initialize(device.Get(), context.Get());
        pipeline.set_processor(std::make_unique<WrongTimestamp>(invalid_resets));
        auto fallback = pipeline.process({texture, {64, 36}, 1234}, {64, 36});
        assert(fallback.fallback && fallback.output.source_timestamp_100ns == 1234 && fallback.output.texture.Get() == texture.Get());
        pipeline.reset(); pipeline.reset();
    }
    assert(invalid_resets == 1);
    texture.Reset(); context->ClearState(); context->Flush(); context.Reset(); device.Reset();
}
