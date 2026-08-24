#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>

namespace fvc {
constexpr wchar_t kMappingName[] = L"Local\\FSR3VirtualCamera_Frame_v1";
constexpr wchar_t kMutexName[]   = L"Local\\FSR3VirtualCamera_FrameMutex_v1";
constexpr wchar_t kEventName[]   = L"Local\\FSR3VirtualCamera_FrameReady_v1";
constexpr uint32_t kMagic = 0x33435646; // FVC3
constexpr uint32_t kMaxWidth = 3840;
constexpr uint32_t kMaxHeight = 2160;
constexpr uint32_t kBytesPerPixel = 4;
constexpr size_t kPixelCapacity = size_t(kMaxWidth) * kMaxHeight * kBytesPerPixel;

#pragma pack(push, 1)
struct SharedFrameHeader {
    uint32_t magic = kMagic;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
    uint32_t fpsNumerator = 60;
    uint32_t fpsDenominator = 1;
    uint64_t frameNumber = 0;
    int64_t qpc = 0;
};
#pragma pack(pop)

class SharedFrameWriter {
public:
    bool Open();
    void Close();
    bool Write(const uint8_t* bgra, uint32_t width, uint32_t height, uint32_t stride, uint64_t frameNo, uint32_t fps = 60);
    ~SharedFrameWriter() { Close(); }
private:
    HANDLE mapping_ = nullptr, mutex_ = nullptr, event_ = nullptr;
    uint8_t* view_ = nullptr;
};

class SharedFrameReader {
public:
    bool Open();
    void Close();
    bool WaitForFrame(DWORD timeoutMs);
    bool Read(std::vector<uint8_t>& bgra, SharedFrameHeader& header);
    ~SharedFrameReader() { Close(); }
private:
    HANDLE mapping_ = nullptr, mutex_ = nullptr, event_ = nullptr;
    uint8_t* view_ = nullptr;
};
}

namespace fvc {
constexpr wchar_t kGpuMappingName[] = L"Local\\FSR3VirtualCamera_GpuFrame_v2";
constexpr wchar_t kGpuMutexName[]   = L"Local\\FSR3VirtualCamera_GpuFrameMutex_v2";
constexpr wchar_t kGpuEventName[]   = L"Local\\FSR3VirtualCamera_GpuFrameReady_v2";
constexpr uint32_t kGpuMagic = 0x47564346; // FVCG

#pragma pack(push, 1)
struct SharedGpuFrameHeader {
    uint32_t magic = kGpuMagic;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t format = 1; // 1 = DXGI_FORMAT_B8G8R8A8_UNORM
    uint32_t fpsNumerator = 60;
    uint32_t fpsDenominator = 1;
    uint64_t frameNumber = 0;
    uint64_t sharedHandle = 0; // legacy D3D11/DXGI shared handle
    int64_t qpc = 0;
};
#pragma pack(pop)

class SharedGpuFrameWriter {
public:
    bool Open();
    void Close();
    bool Publish(uint64_t sharedHandle, uint32_t width, uint32_t height,
                 uint64_t frameNo, uint32_t fps = 60);
    ~SharedGpuFrameWriter() { Close(); }
private:
    HANDLE mapping_ = nullptr, mutex_ = nullptr, event_ = nullptr;
    SharedGpuFrameHeader* view_ = nullptr;
};

class SharedGpuFrameReader {
public:
    bool Open();
    void Close();
    bool WaitForFrame(DWORD timeoutMs);
    bool Read(SharedGpuFrameHeader& header);
    ~SharedGpuFrameReader() { Close(); }
private:
    HANDLE mapping_ = nullptr, mutex_ = nullptr, event_ = nullptr;
    const SharedGpuFrameHeader* view_ = nullptr;
};
}
