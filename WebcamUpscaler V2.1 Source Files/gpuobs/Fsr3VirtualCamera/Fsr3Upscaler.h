#pragma once
#include <windows.h>
#include <d3d11.h>
#include <d3d11on12.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>
#include <chrono>

#if __has_include(<ffx_api.h>)
#   include <ffx_api.h>
#elif __has_include(<ffx_api/ffx_api.h>)
#   include <ffx_api/ffx_api.h>
#else
#   error "AMD FidelityFX core API header not found. Put the SDK in FSR3_SDK."
#endif

// Real AMD FidelityFX FSR 3 upscaling backend.
//
// The camera only supplies colour. FSR 3 is a temporal game upscaler and
// normally expects engine-provided depth, motion vectors and jitter. For a
// webcam, this backend supplies a flat depth buffer and zero motion vectors.
// That means the dispatch is genuinely AMD FSR 3, but temporal reconstruction
// cannot be as informed as an engine integration.
class Fsr3Upscaler {
public:
    bool Initialize(uint32_t inW, uint32_t inH, uint32_t outW, uint32_t outH, std::wstring& error);
    bool UpscaleBGRA(const uint8_t* src, uint32_t srcStride, std::wstring& error);

    bool PublishCurrent(uint64_t& sharedHandle, std::wstring& error);
    bool PublishMidpoint(uint64_t& sharedHandle, std::wstring& error);
    void CommitCurrentAsPrevious();

    void Shutdown();

    bool UsingGpu() const { return device12_ != nullptr; }
    bool UsingFidelityFX() const { return fsrContext_ != nullptr; }
    bool HasPrevious() const { return hasPrevious_; }
    uint32_t OutputWidth() const { return outW_; }
    uint32_t OutputHeight() const { return outH_; }
    const std::wstring& FidelityFXVersion() const { return fsrVersionName_; }
    uint64_t SuccessfulDispatchCount() const { return successfulDispatches_; }
    uint32_t LastDispatchCode() const { return lastDispatchCode_; }
    bool LastDispatchSucceeded() const { return lastDispatchSucceeded_; }

private:
    bool CreateDevices(std::wstring& error);
    bool CreateCommandObjects(std::wstring& error);
    bool CreateFsrResources(std::wstring& error);
    bool CreateInteropResources(std::wstring& error);
    bool CreateShaders(std::wstring& error);
    bool LoadFidelityFX(std::wstring& error);
    bool CreateFidelityFXContext(std::wstring& error);
    bool InitializeStaticTemporalInputs(std::wstring& error);

    bool UploadCameraFrame(const uint8_t* src, uint32_t srcStride, std::wstring& error);
    bool DispatchFsr(std::wstring& error);
    bool ExecuteAndWait(std::wstring& error);
    bool ResetCommandList(std::wstring& error);
    void Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES& tracked,
                    D3D12_RESOURCE_STATES next);

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
    void AcquireFsrOutput();
    void ReleaseFsrOutput();

    uint32_t inW_ = 0, inH_ = 0, outW_ = 0, outH_ = 0;
    bool hasPrevious_ = false;
    bool firstFsrFrame_ = true;
    unsigned sharedIndex_ = 0;
    uint64_t fenceValue_ = 0;
    std::chrono::steady_clock::time_point lastFrameTime_{};

    // DX12 owns the AMD FSR work.
    Microsoft::WRL::ComPtr<ID3D12Device> device12_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue12_;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator12_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list12_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence12_;
    HANDLE fenceEvent_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> input12_;
    Microsoft::WRL::ComPtr<ID3D12Resource> inputUpload12_;
    Microsoft::WRL::ComPtr<ID3D12Resource> depth12_;
    Microsoft::WRL::ComPtr<ID3D12Resource> depthUpload12_;
    Microsoft::WRL::ComPtr<ID3D12Resource> motion12_;
    Microsoft::WRL::ComPtr<ID3D12Resource> motionUpload12_;
    Microsoft::WRL::ComPtr<ID3D12Resource> output12_;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT inputFootprint_{};
    D3D12_RESOURCE_STATES inputState_ = D3D12_RESOURCE_STATE_COPY_DEST;
    D3D12_RESOURCE_STATES depthState_ = D3D12_RESOURCE_STATE_COPY_DEST;
    D3D12_RESOURCE_STATES motionState_ = D3D12_RESOURCE_STATE_COPY_DEST;
    D3D12_RESOURCE_STATES outputState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    // D3D11On12 is only used for the OBS shared texture and the experimental
    // 2x midpoint interpolation. No 4K framebuffer is copied through CPU RAM.
    Microsoft::WRL::ComPtr<ID3D11Device> device11_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context11_;
    Microsoft::WRL::ComPtr<ID3D11On12Device> on12_;
    Microsoft::WRL::ComPtr<ID3D11Resource> outputWrapped11_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> outputSrv11_;

    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> copyPs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> blendPs_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> previous11_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> previousSrv11_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> shared_[2];
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> sharedRtv_[2];
    uint64_t sharedHandle_[2]{};

    // The FSR API is runtime-loaded from AMD's signed loader DLL.
    HMODULE ffxDll_ = nullptr;
    ffxContext fsrContext_ = nullptr;
    PfnFfxCreateContext pCreateContext_ = nullptr;
    PfnFfxDestroyContext pDestroyContext_ = nullptr;
    PfnFfxDispatch pDispatch_ = nullptr;
    PfnFfxQuery pQuery_ = nullptr;
    std::wstring fsrVersionName_;
    uint64_t successfulDispatches_ = 0;
    uint32_t lastDispatchCode_ = 0xFFFFFFFFu;
    bool lastDispatchSucceeded_ = false;
};
