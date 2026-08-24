#include "Fsr3Upscaler.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <vector>

#if __has_include(<ffx_api/ffx_upscale.h>)
#   include <ffx_api/ffx_upscale.h>
#elif __has_include(<ffx_upscale.h>)
#   include <ffx_upscale.h>
#else
#   error "AMD FidelityFX upscaler header not found. Put the FSR SDK in FSR3_SDK."
#endif

#if __has_include(<ffx_api/dx12/ffx_api_dx12.h>)
#   include <ffx_api/dx12/ffx_api_dx12.h>
#elif __has_include(<dx12/ffx_api_dx12.h>)
#   include <dx12/ffx_api_dx12.h>
#else
#   error "AMD FidelityFX DX12 API header not found."
#endif

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

static std::wstring HrText(const wchar_t* what, HRESULT hr)
{
    wchar_t buf[192]{};
    swprintf_s(buf, L"%s (0x%08X)", what, static_cast<unsigned>(hr));
    return buf;
}

static std::wstring FfxText(const wchar_t* what, uint32_t rc)
{
    wchar_t buf[192]{};
    swprintf_s(buf, L"%s (FidelityFX return %u)", what, rc);
    return buf;
}

static const char* kShaderSource = R"(
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
VSOut VSMain(uint id : SV_VertexID)
{
    VSOut o;
    float2 p = id == 0 ? float2(-1,-1) : (id == 1 ? float2(-1,3) : float2(3,-1));
    o.pos = float4(p,0,1);
    o.uv = float2((p.x + 1.0) * 0.5, (1.0 - p.y) * 0.5);
    return o;
}
Texture2D texA : register(t0);
Texture2D texB : register(t1);
SamplerState linearClamp : register(s0);
float4 PSCopy(VSOut i) : SV_TARGET { return texA.Sample(linearClamp, i.uv); }
float4 PSBlend(VSOut i) : SV_TARGET
{
    return lerp(texA.Sample(linearClamp, i.uv), texB.Sample(linearClamp, i.uv), 0.5);
}
)";

static D3D12_HEAP_PROPERTIES HeapProps(D3D12_HEAP_TYPE type)
{
    D3D12_HEAP_PROPERTIES p{};
    p.Type = type;
    p.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    p.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    p.CreationNodeMask = 1;
    p.VisibleNodeMask = 1;
    return p;
}

static D3D12_RESOURCE_DESC Tex2D(DXGI_FORMAT format, uint32_t w, uint32_t h, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE)
{
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = w;
    d.Height = h;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.Format = format;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    d.Flags = flags;
    return d;
}

static D3D12_RESOURCE_DESC Buffer(uint64_t bytes)
{
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = bytes;
    d.Height = 1;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return d;
}

bool Fsr3Upscaler::CreateDevices(std::wstring& error)
{
    ComPtr<IDXGIFactory6> factory;
    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) { error = HrText(L"CreateDXGIFactory2 failed", hr); return false; }

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                         IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) { adapter.Reset(); continue; }
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, _uuidof(ID3D12Device), nullptr))) break;
        adapter.Reset();
    }
    if (!adapter) { error = L"No DirectX 12 hardware adapter supports FSR 3."; return false; }

    hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device12_));
    if (FAILED(hr)) { error = HrText(L"D3D12CreateDevice failed", hr); return false; }

    D3D12_COMMAND_QUEUE_DESC q{};
    q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = device12_->CreateCommandQueue(&q, IID_PPV_ARGS(&queue12_));
    if (FAILED(hr)) { error = HrText(L"CreateCommandQueue failed", hr); return false; }

    IUnknown* queues[] = { queue12_.Get() };
    UINT d3d11Flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    hr = D3D11On12CreateDevice(device12_.Get(), d3d11Flags, nullptr, 0,
                               queues, 1, 0, &device11_, &context11_, nullptr);
    if (FAILED(hr)) { error = HrText(L"D3D11On12CreateDevice failed", hr); return false; }
    hr = device11_.As(&on12_);
    if (FAILED(hr)) { error = HrText(L"Query ID3D11On12Device failed", hr); return false; }
    return true;
}

bool Fsr3Upscaler::CreateCommandObjects(std::wstring& error)
{
    HRESULT hr = device12_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator12_));
    if (FAILED(hr)) { error = HrText(L"CreateCommandAllocator failed", hr); return false; }
    hr = device12_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator12_.Get(), nullptr, IID_PPV_ARGS(&list12_));
    if (FAILED(hr)) { error = HrText(L"CreateCommandList failed", hr); return false; }
    list12_->Close();
    hr = device12_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence12_));
    if (FAILED(hr)) { error = HrText(L"CreateFence failed", hr); return false; }
    fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_) { error = L"CreateEvent for GPU fence failed."; return false; }
    return true;
}

bool Fsr3Upscaler::ResetCommandList(std::wstring& error)
{
    HRESULT hr = allocator12_->Reset();
    if (FAILED(hr)) { error = HrText(L"Command allocator reset failed", hr); return false; }
    hr = list12_->Reset(allocator12_.Get(), nullptr);
    if (FAILED(hr)) { error = HrText(L"Command list reset failed", hr); return false; }
    return true;
}

bool Fsr3Upscaler::ExecuteAndWait(std::wstring& error)
{
    HRESULT hr = list12_->Close();
    if (FAILED(hr)) { error = HrText(L"Command list close failed", hr); return false; }
    ID3D12CommandList* lists[] = { list12_.Get() };
    queue12_->ExecuteCommandLists(1, lists);
    const uint64_t value = ++fenceValue_;
    hr = queue12_->Signal(fence12_.Get(), value);
    if (FAILED(hr)) { error = HrText(L"GPU queue signal failed", hr); return false; }
    if (fence12_->GetCompletedValue() < value) {
        hr = fence12_->SetEventOnCompletion(value, fenceEvent_);
        if (FAILED(hr)) { error = HrText(L"SetEventOnCompletion failed", hr); return false; }
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
    return true;
}

void Fsr3Upscaler::Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES& tracked,
                             D3D12_RESOURCE_STATES next)
{
    if (!resource || tracked == next) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = resource;
    b.Transition.StateBefore = tracked;
    b.Transition.StateAfter = next;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list12_->ResourceBarrier(1, &b);
    tracked = next;
}

bool Fsr3Upscaler::CreateFsrResources(std::wstring& error)
{
    const auto def = HeapProps(D3D12_HEAP_TYPE_DEFAULT);
    const auto upload = HeapProps(D3D12_HEAP_TYPE_UPLOAD);
    HRESULT hr;

    auto inputDesc = Tex2D(DXGI_FORMAT_B8G8R8A8_UNORM, inW_, inH_);
    hr = device12_->CreateCommittedResource(&def, D3D12_HEAP_FLAG_NONE, &inputDesc,
                                            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                            IID_PPV_ARGS(&input12_));
    if (FAILED(hr)) { error = HrText(L"Create FSR input texture failed", hr); return false; }

    UINT rows = 0;
    UINT64 rowBytes = 0, total = 0;
    device12_->GetCopyableFootprints(&inputDesc, 0, 1, 0, &inputFootprint_, &rows, &rowBytes, &total);
    auto uploadDesc = Buffer(total);
    hr = device12_->CreateCommittedResource(&upload, D3D12_HEAP_FLAG_NONE, &uploadDesc,
                                            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                            IID_PPV_ARGS(&inputUpload12_));
    if (FAILED(hr)) { error = HrText(L"Create camera upload buffer failed", hr); return false; }

    auto depthDesc = Tex2D(DXGI_FORMAT_R32_FLOAT, inW_, inH_);
    UINT64 depthBytes = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT depthFp{};
    device12_->GetCopyableFootprints(&depthDesc, 0, 1, 0, &depthFp, nullptr, nullptr, &depthBytes);
    hr = device12_->CreateCommittedResource(&def, D3D12_HEAP_FLAG_NONE, &depthDesc,
                                            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                            IID_PPV_ARGS(&depth12_));
    if (FAILED(hr)) { error = HrText(L"Create compatibility depth texture failed", hr); return false; }
    auto depthUploadDesc = Buffer(depthBytes);
    hr = device12_->CreateCommittedResource(&upload, D3D12_HEAP_FLAG_NONE, &depthUploadDesc,
                                            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                            IID_PPV_ARGS(&depthUpload12_));
    if (FAILED(hr)) { error = HrText(L"Create depth upload buffer failed", hr); return false; }

    auto motionDesc = Tex2D(DXGI_FORMAT_R32G32_FLOAT, inW_, inH_);
    UINT64 motionBytes = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT motionFp{};
    device12_->GetCopyableFootprints(&motionDesc, 0, 1, 0, &motionFp, nullptr, nullptr, &motionBytes);
    hr = device12_->CreateCommittedResource(&def, D3D12_HEAP_FLAG_NONE, &motionDesc,
                                            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                            IID_PPV_ARGS(&motion12_));
    if (FAILED(hr)) { error = HrText(L"Create compatibility motion-vector texture failed", hr); return false; }
    auto motionUploadDesc = Buffer(motionBytes);
    hr = device12_->CreateCommittedResource(&upload, D3D12_HEAP_FLAG_NONE, &motionUploadDesc,
                                            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                            IID_PPV_ARGS(&motionUpload12_));
    if (FAILED(hr)) { error = HrText(L"Create motion-vector upload buffer failed", hr); return false; }

    auto outDesc = Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, outW_, outH_, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    hr = device12_->CreateCommittedResource(&def, D3D12_HEAP_FLAG_NONE, &outDesc,
                                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                            IID_PPV_ARGS(&output12_));
    if (FAILED(hr)) { error = HrText(L"Create FSR output texture failed", hr); return false; }
    return true;
}

bool Fsr3Upscaler::CreateShaders(std::wstring& error)
{
    ComPtr<ID3DBlob> vsBlob, copyBlob, blendBlob, errors;
    HRESULT hr = D3DCompile(kShaderSource, strlen(kShaderSource), "Fsr3ObsInterop", nullptr, nullptr,
                            "VSMain", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsBlob, &errors);
    if (FAILED(hr)) { error = HrText(L"Interop vertex shader compile failed", hr); return false; }
    hr = D3DCompile(kShaderSource, strlen(kShaderSource), "Fsr3ObsInterop", nullptr, nullptr,
                    "PSCopy", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &copyBlob, &errors);
    if (FAILED(hr)) { error = HrText(L"Interop copy shader compile failed", hr); return false; }
    hr = D3DCompile(kShaderSource, strlen(kShaderSource), "Fsr3ObsInterop", nullptr, nullptr,
                    "PSBlend", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blendBlob, &errors);
    if (FAILED(hr)) { error = HrText(L"Interop blend shader compile failed", hr); return false; }
    hr = device11_->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs_);
    if (FAILED(hr)) { error = HrText(L"CreateVertexShader failed", hr); return false; }
    hr = device11_->CreatePixelShader(copyBlob->GetBufferPointer(), copyBlob->GetBufferSize(), nullptr, &copyPs_);
    if (FAILED(hr)) { error = HrText(L"CreatePixelShader failed", hr); return false; }
    hr = device11_->CreatePixelShader(blendBlob->GetBufferPointer(), blendBlob->GetBufferSize(), nullptr, &blendPs_);
    if (FAILED(hr)) { error = HrText(L"CreatePixelShader(blend) failed", hr); return false; }
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    hr = device11_->CreateSamplerState(&sd, &sampler_);
    if (FAILED(hr)) { error = HrText(L"CreateSamplerState failed", hr); return false; }
    return true;
}

bool Fsr3Upscaler::CreateInteropResources(std::wstring& error)
{
    D3D11_RESOURCE_FLAGS wrappedFlags{};
    wrappedFlags.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    HRESULT hr = on12_->CreateWrappedResource(output12_.Get(), &wrappedFlags,
                                               D3D12_RESOURCE_STATE_COPY_SOURCE,
                                               D3D12_RESOURCE_STATE_COPY_SOURCE,
                                               IID_PPV_ARGS(&outputWrapped11_));
    if (FAILED(hr)) { error = HrText(L"Wrap FSR output for OBS interop failed", hr); return false; }
    hr = device11_->CreateShaderResourceView(outputWrapped11_.Get(), nullptr, &outputSrv11_);
    if (FAILED(hr)) { error = HrText(L"Create FSR output SRV failed", hr); return false; }

    D3D11_TEXTURE2D_DESC d{};
    d.Width = outW_;
    d.Height = outH_;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

    hr = device11_->CreateTexture2D(&d, nullptr, &previous11_);
    if (FAILED(hr)) { error = HrText(L"Create previous FSR frame failed", hr); return false; }
    hr = device11_->CreateShaderResourceView(previous11_.Get(), nullptr, &previousSrv11_);
    if (FAILED(hr)) { error = HrText(L"Create previous FSR frame SRV failed", hr); return false; }

    d.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    for (int i = 0; i < 2; ++i) {
        hr = device11_->CreateTexture2D(&d, nullptr, &shared_[i]);
        if (FAILED(hr)) { error = HrText(L"Create shared OBS texture failed", hr); return false; }
        hr = device11_->CreateRenderTargetView(shared_[i].Get(), nullptr, &sharedRtv_[i]);
        if (FAILED(hr)) { error = HrText(L"Create shared OBS RTV failed", hr); return false; }
        ComPtr<IDXGIResource> dxgi;
        hr = shared_[i].As(&dxgi);
        if (FAILED(hr)) { error = HrText(L"Query shared IDXGIResource failed", hr); return false; }
        HANDLE h = nullptr;
        hr = dxgi->GetSharedHandle(&h);
        if (FAILED(hr) || !h) { error = HrText(L"GetSharedHandle for OBS failed", hr); return false; }
        sharedHandle_[i] = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(h));
    }
    return true;
}

bool Fsr3Upscaler::LoadFidelityFX(std::wstring& error)
{
    // FidelityFX SDK v1.1.4 ships the API in one combined signed DX12 DLL.
    const wchar_t* names[] = { L"amd_fidelityfx_dx12.dll" };
    for (auto* name : names) {
        ffxDll_ = LoadLibraryW(name);
        if (ffxDll_) break;
    }
    if (!ffxDll_) {
        error = L"AMD FidelityFX runtime DLL was not found next to WebcamUpscaler. The build copies v1.1.4 amd_fidelityfx_dx12.dll from the SDK ZIP.";
        return false;
    }
    pCreateContext_ = reinterpret_cast<PfnFfxCreateContext>(GetProcAddress(ffxDll_, "ffxCreateContext"));
    pDestroyContext_ = reinterpret_cast<PfnFfxDestroyContext>(GetProcAddress(ffxDll_, "ffxDestroyContext"));
    pDispatch_ = reinterpret_cast<PfnFfxDispatch>(GetProcAddress(ffxDll_, "ffxDispatch"));
    pQuery_ = reinterpret_cast<PfnFfxQuery>(GetProcAddress(ffxDll_, "ffxQuery"));
    if (!pCreateContext_ || !pDestroyContext_ || !pDispatch_ || !pQuery_) {
        error = L"The FidelityFX DLL does not export the FSR API functions.";
        return false;
    }
    return true;
}

bool Fsr3Upscaler::CreateFidelityFXContext(std::wstring& error)
{
    ffxCreateBackendDX12Desc backend{};
    backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
    backend.device = device12_.Get();

    ffxCreateContextDescUpscale create{};
    create.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    create.maxRenderSize = { inW_, inH_ };
    create.maxUpscaleSize = { outW_, outH_ };
    // EOS Webcam Utility delivers display-referred sRGB, not a linear game
    // render target. FidelityFX SDK v1.1.4 exposes an explicit non-linear colour flag for it.
    create.flags = FFX_UPSCALE_ENABLE_AUTO_EXPOSURE | FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE;

    // Enumerate providers and explicitly select FSR 3.x rather than allowing a
    // newer SDK to silently choose FSR 4 as the default upscaler.
    ffxOverrideVersion overrideVersion{};
    bool haveOverride = false;
    uint64_t count = 0;
    ffxQueryDescGetVersions q{};
    q.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
    q.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    q.device = device12_.Get();
    q.outputCount = &count;
    if (pQuery_(nullptr, &q.header) == FFX_API_RETURN_OK && count) {
        std::vector<uint64_t> ids(count);
        std::vector<const char*> names(count);
        q.versionIds = ids.data();
        q.versionNames = names.data();
        if (pQuery_(nullptr, &q.header) == FFX_API_RETURN_OK) {
            for (uint64_t i = 0; i < count; ++i) {
                const std::string n = names[i] ? names[i] : "";
                if (n.find("3.1") != std::string::npos || n.find("FSR3") != std::string::npos || n.find("FSR 3") != std::string::npos) {
                    overrideVersion.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
                    overrideVersion.versionId = ids[i];
                    fsrVersionName_.assign(n.begin(), n.end());
                    haveOverride = true;
                    break;
                }
            }
        }
    }
    if (!haveOverride) {
        error = L"The installed AMD FidelityFX upscaler runtime did not report an FSR 3.x provider. Make sure amd_fidelityfx_dx12.dll from FidelityFX SDK v1.1.4 is beside the EXE.";
        return false;
    }

#if defined(FFX_UPSCALER_VERSION) && defined(FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION)
    ffxCreateContextDescUpscaleVersion apiVersion{};
    apiVersion.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION;
    apiVersion.version = FFX_UPSCALER_VERSION;
    create.header.pNext = &apiVersion.header;
    apiVersion.header.pNext = &overrideVersion.header;
    overrideVersion.header.pNext = &backend.header;
#else
    create.header.pNext = &overrideVersion.header;
    overrideVersion.header.pNext = &backend.header;
#endif

    ffxContext ctx = nullptr;
    const ffxReturnCode_t rc = pCreateContext_(&ctx, &create.header, nullptr);
    if (rc != FFX_API_RETURN_OK || !ctx) {
        error = FfxText(L"Creating the AMD FSR 3 upscaling context failed", static_cast<uint32_t>(rc));
        return false;
    }
    fsrContext_ = ctx;
    return true;
}

bool Fsr3Upscaler::InitializeStaticTemporalInputs(std::wstring& error)
{
    auto fillUpload = [&](ID3D12Resource* upload, const D3D12_RESOURCE_DESC& texDesc,
                          uint32_t bytesPerPixel, const void* pixel, bool repeatPixel,
                          ID3D12Resource* dst, D3D12_RESOURCE_STATES& dstState) -> bool {
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        UINT rows = 0;
        UINT64 rowSize = 0, total = 0;
        device12_->GetCopyableFootprints(&texDesc, 0, 1, 0, &fp, &rows, &rowSize, &total);
        uint8_t* mapped = nullptr;
        D3D12_RANGE noRead{0,0};
        if (FAILED(upload->Map(0, &noRead, reinterpret_cast<void**>(&mapped)))) return false;
        for (UINT y = 0; y < rows; ++y) {
            uint8_t* row = mapped + fp.Offset + static_cast<size_t>(y) * fp.Footprint.RowPitch;
            if (repeatPixel) {
                for (uint32_t x = 0; x < texDesc.Width; ++x) std::memcpy(row + x * bytesPerPixel, pixel, bytesPerPixel);
            } else {
                std::memset(row, 0, static_cast<size_t>(rowSize));
            }
        }
        upload->Unmap(0, nullptr);
        D3D12_TEXTURE_COPY_LOCATION s{};
        s.pResource = upload;
        s.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        s.PlacedFootprint = fp;
        D3D12_TEXTURE_COPY_LOCATION d{};
        d.pResource = dst;
        d.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list12_->CopyTextureRegion(&d, 0, 0, 0, &s, nullptr);
        Transition(dst, dstState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        return true;
    };

    if (!ResetCommandList(error)) return false;
    const float one = 1.0f;
    const float zero2[2] = {0.0f, 0.0f};
    auto depthDesc = depth12_->GetDesc();
    auto motionDesc = motion12_->GetDesc();
    if (!fillUpload(depthUpload12_.Get(), depthDesc, sizeof(float), &one, true, depth12_.Get(), depthState_)) {
        error = L"Could not initialize synthetic webcam depth input."; return false;
    }
    if (!fillUpload(motionUpload12_.Get(), motionDesc, sizeof(zero2), zero2, true, motion12_.Get(), motionState_)) {
        error = L"Could not initialize zero motion-vector input."; return false;
    }
    return ExecuteAndWait(error);
}

bool Fsr3Upscaler::Initialize(uint32_t inW, uint32_t inH, uint32_t outW, uint32_t outH, std::wstring& error)
{
    Shutdown();
    if (!inW || !inH || !outW || !outH) { error = L"Invalid FSR dimensions."; return false; }
    inW_ = inW; inH_ = inH; outW_ = outW; outH_ = outH;
    successfulDispatches_ = 0;
    lastDispatchCode_ = 0xFFFFFFFFu;
    lastDispatchSucceeded_ = false;
    if (!CreateDevices(error) || !CreateCommandObjects(error) || !CreateFsrResources(error) ||
        !CreateShaders(error) || !CreateInteropResources(error) || !LoadFidelityFX(error) ||
        !CreateFidelityFXContext(error) || !InitializeStaticTemporalInputs(error)) {
        Shutdown();
        return false;
    }
    lastFrameTime_ = std::chrono::steady_clock::now();
    return true;
}

bool Fsr3Upscaler::UploadCameraFrame(const uint8_t* src, uint32_t srcStride, std::wstring& error)
{
    if (!src || srcStride < inW_ * 4) { error = L"Invalid webcam frame."; return false; }
    uint8_t* mapped = nullptr;
    D3D12_RANGE noRead{0,0};
    HRESULT hr = inputUpload12_->Map(0, &noRead, reinterpret_cast<void**>(&mapped));
    if (FAILED(hr)) { error = HrText(L"Map camera upload buffer failed", hr); return false; }
    for (uint32_t y = 0; y < inH_; ++y) {
        std::memcpy(mapped + inputFootprint_.Offset + static_cast<size_t>(y) * inputFootprint_.Footprint.RowPitch,
                    src + static_cast<size_t>(y) * srcStride,
                    static_cast<size_t>(inW_) * 4);
    }
    inputUpload12_->Unmap(0, nullptr);

    Transition(input12_.Get(), inputState_, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION s{};
    s.pResource = inputUpload12_.Get();
    s.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    s.PlacedFootprint = inputFootprint_;
    D3D12_TEXTURE_COPY_LOCATION d{};
    d.pResource = input12_.Get();
    d.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list12_->CopyTextureRegion(&d, 0, 0, 0, &s, nullptr);
    Transition(input12_.Get(), inputState_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    return true;
}

bool Fsr3Upscaler::DispatchFsr(std::wstring& error)
{
    Transition(output12_.Get(), outputState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    ffxDispatchDescUpscale d{};
    d.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
    d.commandList = list12_.Get();
    d.color = ffxApiGetResourceDX12(input12_.Get(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
    d.depth = ffxApiGetResourceDX12(depth12_.Get(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
    d.motionVectors = ffxApiGetResourceDX12(motion12_.Get(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
    d.output = ffxApiGetResourceDX12(output12_.Get(), FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    d.exposure = {};
    d.reactive = {};
    d.transparencyAndComposition = {};
    // The webcam image itself cannot be jitter-rendered, so this compatibility
    // integration intentionally reports no camera jitter.
    d.jitterOffset = {0.0f, 0.0f};
    d.motionVectorScale = {static_cast<float>(inW_), static_cast<float>(inH_)};
    d.renderSize = {inW_, inH_};
    d.upscaleSize = {outW_, outH_};
    d.enableSharpening = true;
    d.sharpness = 0.35f;
    const auto now = std::chrono::steady_clock::now();
    float ms = static_cast<float>(std::chrono::duration<double, std::milli>(now - lastFrameTime_).count());
    lastFrameTime_ = now;
    if (!(ms >= 1.0f && ms <= 250.0f)) ms = 33.333f;
    d.frameTimeDelta = ms;
    d.preExposure = 1.0f;
    d.reset = firstFsrFrame_;
    d.cameraNear = 0.1f;
    d.cameraFar = 1000.0f;
    d.cameraFovAngleVertical = 1.0471975512f; // 60 degrees; compatibility value for camera footage.
    d.viewSpaceToMetersFactor = 1.0f;
    d.flags = FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB;

    const ffxReturnCode_t rc = pDispatch_(&fsrContext_, &d.header);
    lastDispatchCode_ = static_cast<uint32_t>(rc);
    lastDispatchSucceeded_ = (rc == FFX_API_RETURN_OK);
    if (!lastDispatchSucceeded_) {
        error = FfxText(L"AMD FSR 3 dispatch failed", lastDispatchCode_);
        return false;
    }
    ++successfulDispatches_;
    firstFsrFrame_ = false;

    // The interop wrapper expects COPY_SOURCE when D3D11On12 acquires it.
    Transition(output12_.Get(), outputState_, D3D12_RESOURCE_STATE_COPY_SOURCE);
    return true;
}

bool Fsr3Upscaler::UpscaleBGRA(const uint8_t* src, uint32_t srcStride, std::wstring& error)
{
    if (!fsrContext_ || !device12_) { error = L"AMD FSR 3 is not initialized."; return false; }
    if (!ResetCommandList(error)) return false;
    if (!UploadCameraFrame(src, srcStride, error)) return false;
    if (!DispatchFsr(error)) return false;
    return ExecuteAndWait(error);
}

void Fsr3Upscaler::AcquireFsrOutput()
{
    ID3D11Resource* r = outputWrapped11_.Get();
    on12_->AcquireWrappedResources(&r, 1);
}

void Fsr3Upscaler::ReleaseFsrOutput()
{
    ID3D11Resource* r = outputWrapped11_.Get();
    on12_->ReleaseWrappedResources(&r, 1);
    context11_->Flush();
}

bool Fsr3Upscaler::Draw(ID3D11RenderTargetView* target, ID3D11ShaderResourceView* a,
                        ID3D11ShaderResourceView* b, bool blend,
                        uint32_t width, uint32_t height, std::wstring& error)
{
    if (!target || !a) { error = L"OBS interop draw received a null resource."; return false; }
    ID3D11RenderTargetView* rt = target;
    context11_->OMSetRenderTargets(1, &rt, nullptr);
    D3D11_VIEWPORT vp{};
    vp.Width = static_cast<float>(width); vp.Height = static_cast<float>(height); vp.MaxDepth = 1.0f;
    context11_->RSSetViewports(1, &vp);
    context11_->IASetInputLayout(nullptr);
    context11_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context11_->VSSetShader(vs_.Get(), nullptr, 0);
    context11_->PSSetShader(blend ? blendPs_.Get() : copyPs_.Get(), nullptr, 0);
    ID3D11ShaderResourceView* srvs[2] = { a, b };
    context11_->PSSetShaderResources(0, 2, srvs);
    ID3D11SamplerState* s = sampler_.Get();
    context11_->PSSetSamplers(0, 1, &s);
    context11_->Draw(3, 0);
    ID3D11ShaderResourceView* nil[2] = { nullptr, nullptr };
    context11_->PSSetShaderResources(0, 2, nil);
    context11_->OMSetRenderTargets(0, nullptr, nullptr);
    return true;
}

bool Fsr3Upscaler::Publish(ID3D11ShaderResourceView* a, ID3D11ShaderResourceView* b,
                           bool blend, uint64_t& sharedHandle, std::wstring& error)
{
    sharedIndex_ ^= 1u;
    if (!Draw(sharedRtv_[sharedIndex_].Get(), a, b, blend, outW_, outH_, error)) return false;
    context11_->Flush();
    sharedHandle = sharedHandle_[sharedIndex_];
    return sharedHandle != 0;
}

bool Fsr3Upscaler::PublishCurrent(uint64_t& sharedHandle, std::wstring& error)
{
    AcquireFsrOutput();
    const bool ok = Publish(outputSrv11_.Get(), nullptr, false, sharedHandle, error);
    ReleaseFsrOutput();
    return ok;
}

bool Fsr3Upscaler::PublishMidpoint(uint64_t& sharedHandle, std::wstring& error)
{
    if (!hasPrevious_) { error = L"No previous FSR frame exists for interpolation."; return false; }
    AcquireFsrOutput();
    const bool ok = Publish(previousSrv11_.Get(), outputSrv11_.Get(), true, sharedHandle, error);
    ReleaseFsrOutput();
    return ok;
}

void Fsr3Upscaler::CommitCurrentAsPrevious()
{
    if (!outputWrapped11_ || !previous11_) return;
    AcquireFsrOutput();
    context11_->CopyResource(previous11_.Get(), outputWrapped11_.Get());
    context11_->Flush();
    ReleaseFsrOutput();
    hasPrevious_ = true;
}

void Fsr3Upscaler::Shutdown()
{
    if (queue12_ && fence12_) {
        const uint64_t value = ++fenceValue_;
        if (SUCCEEDED(queue12_->Signal(fence12_.Get(), value)) && fence12_->GetCompletedValue() < value && fenceEvent_) {
            if (SUCCEEDED(fence12_->SetEventOnCompletion(value, fenceEvent_))) WaitForSingleObject(fenceEvent_, INFINITE);
        }
    }
    if (fsrContext_ && pDestroyContext_) {
        pDestroyContext_(&fsrContext_, nullptr);
        fsrContext_ = nullptr;
    }
    if (ffxDll_) { FreeLibrary(ffxDll_); ffxDll_ = nullptr; }
    pCreateContext_ = nullptr; pDestroyContext_ = nullptr; pDispatch_ = nullptr; pQuery_ = nullptr;
    fsrVersionName_.clear();
    successfulDispatches_ = 0;
    lastDispatchCode_ = 0xFFFFFFFFu;
    lastDispatchSucceeded_ = false;

    outputSrv11_.Reset(); outputWrapped11_.Reset(); previousSrv11_.Reset(); previous11_.Reset();
    for (int i = 0; i < 2; ++i) { sharedRtv_[i].Reset(); shared_[i].Reset(); sharedHandle_[i] = 0; }
    sampler_.Reset(); blendPs_.Reset(); copyPs_.Reset(); vs_.Reset(); on12_.Reset(); context11_.Reset(); device11_.Reset();
    output12_.Reset(); motionUpload12_.Reset(); motion12_.Reset(); depthUpload12_.Reset(); depth12_.Reset(); inputUpload12_.Reset(); input12_.Reset();
    list12_.Reset(); allocator12_.Reset(); queue12_.Reset(); fence12_.Reset(); device12_.Reset();
    if (fenceEvent_) { CloseHandle(fenceEvent_); fenceEvent_ = nullptr; }
    hasPrevious_ = false; firstFsrFrame_ = true; sharedIndex_ = 0; fenceValue_ = 0;
    inW_ = inH_ = outW_ = outH_ = 0;
}
