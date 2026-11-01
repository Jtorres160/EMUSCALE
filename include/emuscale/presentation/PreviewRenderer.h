#pragma once
#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.h>
#include <wrl/client.h>
#include <string>

namespace emuscale {
class PreviewRenderer {
public:
    void initialize(HWND window);
    void resize(UINT width, UINT height);
    void present(ID3D11Texture2D* source, UINT content_width, UINT content_height);
    [[nodiscard]] ID3D11Device* device() const noexcept { return device_.Get(); }
    [[nodiscard]] const std::wstring& gpu_name() const noexcept { return gpu_name_; }
    [[nodiscard]] UINT width() const noexcept { return width_; }
    [[nodiscard]] UINT height() const noexcept { return height_; }

private:
    void create_target();
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_chain_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> copy_texture_;
    UINT width_ = 0;
    UINT height_ = 0;
    std::wstring gpu_name_;
};
} // namespace emuscale
