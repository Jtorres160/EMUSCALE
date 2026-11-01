#include "emuscale/presentation/PreviewRenderer.h"

#include <d3dcompiler.h>
#include <dxgi1_5.h>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <sstream>

namespace emuscale {
namespace {
void check(HRESULT hr, const char* operation) {
    if (FAILED(hr)) {
        std::ostringstream message;
        message << operation << " failed (HRESULT 0x" << std::hex
                << static_cast<unsigned long>(hr) << ')';
        throw std::runtime_error(message.str());
    }
}

constexpr char shader[] = R"(
struct VertexOutput { float4 position : SV_Position; float2 uv : TEXCOORD0; };
VertexOutput vs_main(uint id : SV_VertexID) {
    float2 p[3] = { float2(-1,-1), float2(-1,3), float2(3,-1) };
    VertexOutput o;
    o.position = float4(p[id], 0, 1);
    o.uv = float2((p[id].x + 1) * 0.5, (1 - p[id].y) * 0.5);
    return o;
}
Texture2D image : register(t0);
SamplerState linear_sampler : register(s0);
cbuffer SizeConstants : register(b0) { float2 uv_scale; float2 padding; };
float4 ps_main(VertexOutput input) : SV_Target {
    return image.Sample(linear_sampler, input.uv * uv_scale);
}
)";

Microsoft::WRL::ComPtr<ID3DBlob> compile(const char* entry, const char* target) {
    Microsoft::WRL::ComPtr<ID3DBlob> blob, errors;
    const HRESULT hr = D3DCompile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr,
                                  entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                                  &blob, &errors);
    if (FAILED(hr)) {
        std::string detail = errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()),
                                                  errors->GetBufferSize()) : "Unknown shader error";
        throw std::runtime_error(detail);
    }
    return blob;
}
} // namespace

void PreviewRenderer::initialize(HWND window) {
    if (!window) throw std::invalid_argument("Preview window is null");
    D3D_FEATURE_LEVEL level{};
    constexpr D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    auto create_device = [&](IDXGIAdapter* adapter) {
        auto driver = adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;
        HRESULT result = D3D11CreateDevice(adapter, driver, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 2, D3D11_SDK_VERSION,
            &device_, &level, &context_);
        if (result == E_INVALIDARG)
            result = D3D11CreateDevice(adapter, driver, nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels + 1, 1,
                D3D11_SDK_VERSION, &device_, &level, &context_);
        return result;
    };
    HRESULT hr = E_FAIL;
    Microsoft::WRL::ComPtr<IDXGIFactory1> inventory;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&inventory)))) {
        for (UINT i = 0;; ++i) {
            Microsoft::WRL::ComPtr<IDXGIAdapter1> candidate;
            const HRESULT enumerated = inventory->EnumAdapters1(i, &candidate);
            if (FAILED(enumerated)) break;
            DXGI_ADAPTER_DESC1 description{};
            if (FAILED(candidate->GetDesc1(&description)) || description.VendorId != 0x10DE ||
                (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) continue;
            hr = create_device(candidate.Get());
            if (SUCCEEDED(hr)) break;
        }
    }
    if (!device_) hr = create_device(nullptr);
    check(hr, "D3D11CreateDevice");

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
    check(device_.As(&dxgi), "IDXGIDevice query");
    check(dxgi->GetAdapter(&adapter), "GetAdapter");
    DXGI_ADAPTER_DESC description{};
    check(adapter->GetDesc(&description), "GetDesc");
    gpu_name_ = description.Description;
    check(adapter->GetParent(IID_PPV_ARGS(&factory)), "IDXGIFactory2 query");
    Microsoft::WRL::ComPtr<IDXGIFactory5> factory5;
    if (SUCCEEDED(factory.As(&factory5))) {
        BOOL supported = FALSE;
        tearing_supported_ = SUCCEEDED(factory5->CheckFeatureSupport(
            DXGI_FEATURE_PRESENT_ALLOW_TEARING, &supported, sizeof(supported))) && supported;
    }

    RECT rect{};
    GetClientRect(window, &rect);
    width_ = std::max<LONG>(1, rect.right - rect.left);
    height_ = std::max<LONG>(1, rect.bottom - rect.top);
    DXGI_SWAP_CHAIN_DESC1 chain{};
    chain.Width = width_;
    chain.Height = height_;
    chain.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    chain.SampleDesc.Count = 1;
    chain.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    chain.BufferCount = 2;
    chain.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    chain.Flags = tearing_supported_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    check(factory->CreateSwapChainForHwnd(device_.Get(), window, &chain, nullptr,
                                          nullptr, &swap_chain_), "CreateSwapChainForHwnd");
    factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
    create_target();

    auto vs = compile("vs_main", "vs_5_0");
    auto ps = compile("ps_main", "ps_5_0");
    check(device_->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(),
                                      nullptr, &vertex_shader_), "CreateVertexShader");
    check(device_->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(),
                                     nullptr, &pixel_shader_), "CreatePixelShader");
    D3D11_SAMPLER_DESC sample{};
    sample.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sample.AddressU = sample.AddressV = sample.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sample.MaxLOD = D3D11_FLOAT32_MAX;
    check(device_->CreateSamplerState(&sample, &sampler_), "CreateSamplerState");
    D3D11_BUFFER_DESC constant{};
    constant.ByteWidth = 16;
    constant.Usage = D3D11_USAGE_DEFAULT;
    constant.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    check(device_->CreateBuffer(&constant, nullptr, &constants_), "CreateBuffer");
}

void PreviewRenderer::create_target() {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backbuffer;
    check(swap_chain_->GetBuffer(0, IID_PPV_ARGS(&backbuffer)), "GetBuffer");
    check(device_->CreateRenderTargetView(backbuffer.Get(), nullptr, &target_),
          "CreateRenderTargetView");
}

void PreviewRenderer::resize(UINT width, UINT height) {
    if (!swap_chain_ || width == 0 || height == 0 || (width == width_ && height == height_)) return;
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    target_.Reset();
    context_->ClearState();
    check(swap_chain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN,
        tearing_supported_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0), "ResizeBuffers");
    width_ = width;
    height_ = height;
    create_target();
}

void PreviewRenderer::present(ID3D11Texture2D* source, UINT content_width, UINT content_height) {
    if (!source || !target_ || !content_width || !content_height) return;
    D3D11_TEXTURE2D_DESC source_desc{};
    source->GetDesc(&source_desc);
    content_width = std::min(content_width, source_desc.Width);
    content_height = std::min(content_height, source_desc.Height);
    if (!content_width || !content_height) return;

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    HRESULT hr = device_->CreateShaderResourceView(source, nullptr, &view);
    if (FAILED(hr)) {
        D3D11_TEXTURE2D_DESC copy_desc = source_desc;
        copy_desc.Usage = D3D11_USAGE_DEFAULT;
        copy_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        copy_desc.CPUAccessFlags = 0;
        copy_desc.MiscFlags = 0;
        copy_desc.MipLevels = 1;
        copy_desc.ArraySize = 1;
        D3D11_TEXTURE2D_DESC existing{};
        if (copy_texture_) copy_texture_->GetDesc(&existing);
        if (!copy_texture_ || existing.Width != copy_desc.Width ||
            existing.Height != copy_desc.Height || existing.Format != copy_desc.Format) {
            copy_texture_.Reset();
            check(device_->CreateTexture2D(&copy_desc, nullptr, &copy_texture_), "CreateTexture2D");
        }
        context_->CopyResource(copy_texture_.Get(), source);
        check(device_->CreateShaderResourceView(copy_texture_.Get(), nullptr, &view),
              "CreateShaderResourceView");
    }

    const float src_aspect = static_cast<float>(content_width) / content_height;
    const float dst_aspect = static_cast<float>(width_) / height_;
    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(width_);
    viewport.Height = static_cast<float>(height_);
    if (dst_aspect > src_aspect) viewport.Width = viewport.Height * src_aspect;
    else viewport.Height = viewport.Width / src_aspect;
    viewport.TopLeftX = (width_ - viewport.Width) * 0.5f;
    viewport.TopLeftY = (height_ - viewport.Height) * 0.5f;
    viewport.MaxDepth = 1.0f;
    struct { float u, v, pad0, pad1; } scales{
        static_cast<float>(content_width) / source_desc.Width,
        static_cast<float>(content_height) / source_desc.Height, 0, 0};
    context_->UpdateSubresource(constants_.Get(), 0, nullptr, &scales, 0, 0);
    constexpr float black[] = {0, 0, 0, 1};
    context_->ClearRenderTargetView(target_.Get(), black);
    auto* render_target = target_.Get();
    auto* resource_view = view.Get();
    auto* sampler = sampler_.Get();
    auto* constants = constants_.Get();
    context_->OMSetRenderTargets(1, &render_target, nullptr);
    context_->RSSetViewports(1, &viewport);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertex_shader_.Get(), nullptr, 0);
    context_->PSSetShader(pixel_shader_.Get(), nullptr, 0);
    context_->PSSetShaderResources(0, 1, &resource_view);
    context_->PSSetSamplers(0, 1, &sampler);
    context_->PSSetConstantBuffers(0, 1, &constants);
    context_->Draw(3, 0);
    ID3D11ShaderResourceView* empty = nullptr;
    context_->PSSetShaderResources(0, 1, &empty);
    const auto policy = present_policy(vsync_, tearing_supported_);
    check(swap_chain_->Present(policy.sync_interval,
        policy.allow_tearing ? DXGI_PRESENT_ALLOW_TEARING : 0), "Present");
}
} // namespace emuscale
