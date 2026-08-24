#include "Fsr3Upscaler.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <sstream>
#include <cstring>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

static std::wstring HrText(const wchar_t* what, HRESULT hr)
{
    wchar_t buf[128]{};
    swprintf_s(buf, L"%s (0x%08X)", what, static_cast<unsigned>(hr));
    return buf;
}

static const char* kShaderSource = R"(
struct VSOut {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VertexID)
{
    VSOut o;
    float2 p;
    if (id == 0) p = float2(-1.0, -1.0);
    else if (id == 1) p = float2(-1.0,  3.0);
    else p = float2(3.0, -1.0);
    o.pos = float4(p, 0.0, 1.0);
    o.uv = float2((p.x + 1.0) * 0.5, (1.0 - p.y) * 0.5);
    return o;
}

Texture2D texA : register(t0);
Texture2D texB : register(t1);
SamplerState linearClamp : register(s0);

float4 PSCopy(VSOut i) : SV_TARGET
{
    return texA.Sample(linearClamp, i.uv);
}

float4 PSBlend(VSOut i) : SV_TARGET
{
    return lerp(texA.Sample(linearClamp, i.uv),
                texB.Sample(linearClamp, i.uv), 0.5);
}
)";

bool Fsr3Upscaler::CreateShaders(std::wstring& error)
{
    ComPtr<ID3DBlob> vsBlob, copyBlob, blendBlob, errors;
    HRESULT hr = D3DCompile(kShaderSource, strlen(kShaderSource), "GpuCameraScaler", nullptr, nullptr,
                            "VSMain", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                            &vsBlob, &errors);
    if (FAILED(hr)) { error = HrText(L"Vertex shader compile failed", hr); return false; }

    errors.Reset();
    hr = D3DCompile(kShaderSource, strlen(kShaderSource), "GpuCameraScaler", nullptr, nullptr,
                    "PSCopy", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                    &copyBlob, &errors);
    if (FAILED(hr)) { error = HrText(L"Copy shader compile failed", hr); return false; }

    errors.Reset();
    hr = D3DCompile(kShaderSource, strlen(kShaderSource), "GpuCameraScaler", nullptr, nullptr,
                    "PSBlend", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                    &blendBlob, &errors);
    if (FAILED(hr)) { error = HrText(L"Blend shader compile failed", hr); return false; }

    hr = device_->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs_);
    if (FAILED(hr)) { error = HrText(L"CreateVertexShader failed", hr); return false; }
    hr = device_->CreatePixelShader(copyBlob->GetBufferPointer(), copyBlob->GetBufferSize(), nullptr, &copyPs_);
    if (FAILED(hr)) { error = HrText(L"CreatePixelShader failed", hr); return false; }
    hr = device_->CreatePixelShader(blendBlob->GetBufferPointer(), blendBlob->GetBufferSize(), nullptr, &blendPs_);
    if (FAILED(hr)) { error = HrText(L"CreatePixelShader(blend) failed", hr); return false; }

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    hr = device_->CreateSamplerState(&sd, &sampler_);
    if (FAILED(hr)) { error = HrText(L"CreateSamplerState failed", hr); return false; }
    return true;
}

bool Fsr3Upscaler::CreateTextures(std::wstring& error)
{
    HRESULT hr;

    D3D11_TEXTURE2D_DESC inDesc{};
    inDesc.Width = inW_;
    inDesc.Height = inH_;
    inDesc.MipLevels = 1;
    inDesc.ArraySize = 1;
    inDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    inDesc.SampleDesc.Count = 1;
    inDesc.Usage = D3D11_USAGE_DEFAULT;
    inDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    hr = device_->CreateTexture2D(&inDesc, nullptr, &input_);
    if (FAILED(hr)) { error = HrText(L"Create input GPU texture failed", hr); return false; }
    hr = device_->CreateShaderResourceView(input_.Get(), nullptr, &inputSrv_);
    if (FAILED(hr)) { error = HrText(L"Create input SRV failed", hr); return false; }

    D3D11_TEXTURE2D_DESC outDesc{};
    outDesc.Width = outW_;
    outDesc.Height = outH_;
    outDesc.MipLevels = 1;
    outDesc.ArraySize = 1;
    outDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    outDesc.SampleDesc.Count = 1;
    outDesc.Usage = D3D11_USAGE_DEFAULT;
    outDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    hr = device_->CreateTexture2D(&outDesc, nullptr, &current_);
    if (FAILED(hr)) { error = HrText(L"Create current texture failed", hr); return false; }
    hr = device_->CreateRenderTargetView(current_.Get(), nullptr, &currentRtv_);
    if (FAILED(hr)) { error = HrText(L"Create current RTV failed", hr); return false; }
    hr = device_->CreateShaderResourceView(current_.Get(), nullptr, &currentSrv_);
    if (FAILED(hr)) { error = HrText(L"Create current SRV failed", hr); return false; }

    hr = device_->CreateTexture2D(&outDesc, nullptr, &previous_);
    if (FAILED(hr)) { error = HrText(L"Create previous texture failed", hr); return false; }
    hr = device_->CreateShaderResourceView(previous_.Get(), nullptr, &previousSrv_);
    if (FAILED(hr)) { error = HrText(L"Create previous SRV failed", hr); return false; }

    outDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    for (int i = 0; i < 2; ++i) {
        hr = device_->CreateTexture2D(&outDesc, nullptr, &shared_[i]);
        if (FAILED(hr)) { error = HrText(L"Create shared OBS texture failed", hr); return false; }
        hr = device_->CreateRenderTargetView(shared_[i].Get(), nullptr, &sharedRtv_[i]);
        if (FAILED(hr)) { error = HrText(L"Create shared RTV failed", hr); return false; }

        ComPtr<IDXGIResource> resource;
        hr = shared_[i].As(&resource);
        if (FAILED(hr)) { error = HrText(L"Query IDXGIResource failed", hr); return false; }
        HANDLE handle = nullptr;
        hr = resource->GetSharedHandle(&handle);
        if (FAILED(hr) || !handle) { error = HrText(L"GetSharedHandle failed", hr); return false; }
        sharedHandle_[i] = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(handle));
    }
    return true;
}

bool Fsr3Upscaler::Initialize(uint32_t inW, uint32_t inH, uint32_t outW, uint32_t outH, std::wstring& error)
{
    Shutdown();
    if (!inW || !inH || !outW || !outH) { error = L"Invalid dimensions"; return false; }
    inW_ = inW; inH_ = inH; outW_ = outW; outH_ = outH;

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    // Do not require the debug layer: many normal Windows installs do not have it.
#endif
    D3D_FEATURE_LEVEL requested[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL obtained{};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                   requested, ARRAYSIZE(requested), D3D11_SDK_VERSION,
                                   &device_, &obtained, &context_);
    if (hr == E_INVALIDARG) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                               requested + 1, ARRAYSIZE(requested) - 1, D3D11_SDK_VERSION,
                               &device_, &obtained, &context_);
    }
    if (FAILED(hr)) { error = HrText(L"D3D11 hardware device creation failed", hr); Shutdown(); return false; }

    if (!CreateShaders(error) || !CreateTextures(error)) { Shutdown(); return false; }
    return true;
}

bool Fsr3Upscaler::Draw(ID3D11RenderTargetView* target,
                        ID3D11ShaderResourceView* a,
                        ID3D11ShaderResourceView* b,
                        bool blend,
                        uint32_t width,
                        uint32_t height,
                        std::wstring& error)
{
    if (!target || !a) { error = L"GPU draw received a null resource"; return false; }
    ID3D11RenderTargetView* rt = target;
    context_->OMSetRenderTargets(1, &rt, nullptr);

    D3D11_VIEWPORT vp{};
    vp.Width = static_cast<float>(width);
    vp.Height = static_cast<float>(height);
    vp.MaxDepth = 1.0f;
    context_->RSSetViewports(1, &vp);

    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vs_.Get(), nullptr, 0);
    context_->PSSetShader(blend ? blendPs_.Get() : copyPs_.Get(), nullptr, 0);
    ID3D11ShaderResourceView* srvs[2] = { a, b };
    context_->PSSetShaderResources(0, 2, srvs);
    ID3D11SamplerState* sampler = sampler_.Get();
    context_->PSSetSamplers(0, 1, &sampler);
    context_->Draw(3, 0);

    ID3D11ShaderResourceView* nullSrvs[2] = { nullptr, nullptr };
    context_->PSSetShaderResources(0, 2, nullSrvs);
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    return true;
}

bool Fsr3Upscaler::UpscaleBGRA(const uint8_t* src, uint32_t srcStride, std::wstring& error)
{
    if (!device_ || !src || srcStride < inW_ * 4) { error = L"Bad source frame or GPU not initialized"; return false; }
    context_->UpdateSubresource(input_.Get(), 0, nullptr, src, srcStride, 0);
    return Draw(currentRtv_.Get(), inputSrv_.Get(), nullptr, false, outW_, outH_, error);
}

bool Fsr3Upscaler::Publish(ID3D11ShaderResourceView* a,
                           ID3D11ShaderResourceView* b,
                           bool blend,
                           uint64_t& sharedHandle,
                           std::wstring& error)
{
    sharedIndex_ ^= 1u;
    if (!Draw(sharedRtv_[sharedIndex_].Get(), a, b, blend, outW_, outH_, error))
        return false;

    // Shared D3D11 resources require the producer to flush before another
    // device/process consumes the new contents.
    context_->Flush();
    sharedHandle = sharedHandle_[sharedIndex_];
    return sharedHandle != 0;
}

bool Fsr3Upscaler::PublishCurrent(uint64_t& sharedHandle, std::wstring& error)
{
    return Publish(currentSrv_.Get(), nullptr, false, sharedHandle, error);
}

bool Fsr3Upscaler::PublishMidpoint(uint64_t& sharedHandle, std::wstring& error)
{
    if (!hasPrevious_) { error = L"No previous GPU frame exists yet"; return false; }
    return Publish(previousSrv_.Get(), currentSrv_.Get(), true, sharedHandle, error);
}

void Fsr3Upscaler::CommitCurrentAsPrevious()
{
    if (!context_ || !current_ || !previous_) return;
    context_->CopyResource(previous_.Get(), current_.Get());
    hasPrevious_ = true;
}

void Fsr3Upscaler::Shutdown()
{
    if (context_) context_->ClearState();
    for (int i = 0; i < 2; ++i) {
        sharedRtv_[i].Reset();
        shared_[i].Reset();
        sharedHandle_[i] = 0;
    }
    previousSrv_.Reset(); previous_.Reset();
    currentSrv_.Reset(); currentRtv_.Reset(); current_.Reset();
    inputSrv_.Reset(); input_.Reset();
    sampler_.Reset(); blendPs_.Reset(); copyPs_.Reset(); vs_.Reset();
    context_.Reset(); device_.Reset();
    hasPrevious_ = false;
    sharedIndex_ = 0;
    inW_ = inH_ = outW_ = outH_ = 0;
}
