#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>

// GPU scaler/output stage.
//
// This revision deliberately moves the expensive resize and the OBS handoff to
// D3D11. The webcam may still arrive through system memory when the selected
// camera/virtual-camera source does not expose a DXGI-backed sample, but after
// the upload no 4K CPU framebuffer is produced for OBS.
//
// FidelityFX remains a separate future backend: arbitrary camera video does not
// supply the temporal depth/motion inputs required by FSR Super Resolution.
class Fsr3Upscaler {
public:
    bool Initialize(uint32_t inW, uint32_t inH, uint32_t outW, uint32_t outH, std::wstring& error);
    bool UpscaleBGRA(const uint8_t* src, uint32_t srcStride, std::wstring& error);

    // Publishes the current upscaled texture into one of two shared D3D11
    // textures. Returns the legacy DXGI shared handle OBS can open.
    bool PublishCurrent(uint64_t& sharedHandle, std::wstring& error);

    // Experimental camera frame generation: renders a 50/50 midpoint between
    // the previous and current upscaled frames entirely on the GPU.
    bool PublishMidpoint(uint64_t& sharedHandle, std::wstring& error);
    void CommitCurrentAsPrevious();

    void Shutdown();

    bool UsingGpu() const { return device_ != nullptr; }
    bool UsingFidelityFX() const { return false; }
    bool HasPrevious() const { return hasPrevious_; }
    uint32_t OutputWidth() const { return outW_; }
    uint32_t OutputHeight() const { return outH_; }

private:
    bool CreateShaders(std::wstring& error);
    bool CreateTextures(std::wstring& error);
    bool Draw(ID3D11RenderTargetView* target,
              ID3D11ShaderResourceView* a,
              ID3D11ShaderResourceView* b,
              bool blend,
              uint32_t width,
              uint32_t height,
              std::wstring& error);
    bool Publish(ID3D11ShaderResourceView* a,
                 ID3D11ShaderResourceView* b,
                 bool blend,
                 uint64_t& sharedHandle,
                 std::wstring& error);

    uint32_t inW_ = 0, inH_ = 0, outW_ = 0, outH_ = 0;
    bool hasPrevious_ = false;
    unsigned sharedIndex_ = 0;

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> copyPs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> blendPs_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> input_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> inputSrv_;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> current_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> currentRtv_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> currentSrv_;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> previous_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> previousSrv_;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> shared_[2];
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> sharedRtv_[2];
    uint64_t sharedHandle_[2]{};
};
