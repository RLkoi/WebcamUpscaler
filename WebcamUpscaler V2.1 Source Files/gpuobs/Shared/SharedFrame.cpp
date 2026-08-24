#include "SharedFrame.h"
#include <vector>
#include <cstring>

namespace fvc {
static constexpr size_t kMappingSize = sizeof(SharedFrameHeader) + kPixelCapacity;

bool SharedFrameWriter::Open() {
    Close();
    mutex_ = CreateMutexW(nullptr, FALSE, kMutexName);
    event_ = CreateEventW(nullptr, FALSE, FALSE, kEventName); // auto-reset, one wake per published frame
    mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
        DWORD((kMappingSize >> 32) & 0xffffffff), DWORD(kMappingSize & 0xffffffff), kMappingName);
    if (!mapping_) return false;
    view_ = static_cast<uint8_t*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, kMappingSize));
    return view_ != nullptr;
}
void SharedFrameWriter::Close() {
    if (view_) { UnmapViewOfFile(view_); view_ = nullptr; }
    if (mapping_) { CloseHandle(mapping_); mapping_ = nullptr; }
    if (event_) { CloseHandle(event_); event_ = nullptr; }
    if (mutex_) { CloseHandle(mutex_); mutex_ = nullptr; }
}
bool SharedFrameWriter::Write(const uint8_t* bgra, uint32_t w, uint32_t h, uint32_t stride, uint64_t frameNo, uint32_t fps) {
    if (!view_ || !bgra || !w || !h || w > kMaxWidth || h > kMaxHeight || stride < w * 4) return false;
    if (WaitForSingleObject(mutex_, 30) != WAIT_OBJECT_0) return false;
    auto* hdr = reinterpret_cast<SharedFrameHeader*>(view_);
    hdr->magic = kMagic; hdr->width = w; hdr->height = h; hdr->stride = w * 4;
    hdr->fpsNumerator = fps; hdr->fpsDenominator = 1; hdr->frameNumber = frameNo;
    LARGE_INTEGER q{}; QueryPerformanceCounter(&q); hdr->qpc = q.QuadPart;
    uint8_t* dst = view_ + sizeof(SharedFrameHeader);
    for (uint32_t y=0; y<h; ++y) std::memcpy(dst + size_t(y)*w*4, bgra + size_t(y)*stride, size_t(w)*4);
    ReleaseMutex(mutex_);
    if (event_) SetEvent(event_);
    return true;
}

bool SharedFrameReader::Open() {
    Close();
    mutex_ = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, kMutexName);
    event_ = OpenEventW(SYNCHRONIZE, FALSE, kEventName);
    mapping_ = OpenFileMappingW(FILE_MAP_READ, FALSE, kMappingName);
    if (!mapping_) return false;
    view_ = static_cast<uint8_t*>(MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, kMappingSize));
    return view_ != nullptr;
}
void SharedFrameReader::Close() {
    if (view_) { UnmapViewOfFile(view_); view_ = nullptr; }
    if (mapping_) { CloseHandle(mapping_); mapping_ = nullptr; }
    if (event_) { CloseHandle(event_); event_ = nullptr; }
    if (mutex_) { CloseHandle(mutex_); mutex_ = nullptr; }
}
bool SharedFrameReader::WaitForFrame(DWORD timeoutMs) {
    return event_ ? WaitForSingleObject(event_, timeoutMs) == WAIT_OBJECT_0 : false;
}
bool SharedFrameReader::Read(std::vector<uint8_t>& bgra, SharedFrameHeader& out) {
    if (!view_) return false;
    if (mutex_ && WaitForSingleObject(mutex_, 20) != WAIT_OBJECT_0) return false;
    auto hdr = *reinterpret_cast<const SharedFrameHeader*>(view_);
    if (hdr.magic != kMagic || !hdr.width || !hdr.height || hdr.width > kMaxWidth || hdr.height > kMaxHeight) {
        if (mutex_) ReleaseMutex(mutex_);
        return false;
    }
    bgra.resize(size_t(hdr.width)*hdr.height*4);
    std::memcpy(bgra.data(), view_ + sizeof(SharedFrameHeader), bgra.size());
    out = hdr;
    if (mutex_) ReleaseMutex(mutex_);
    return true;
}
}

namespace fvc {
bool SharedGpuFrameWriter::Open() {
    Close();
    mutex_ = CreateMutexW(nullptr, FALSE, kGpuMutexName);
    event_ = CreateEventW(nullptr, FALSE, FALSE, kGpuEventName);
    mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                  static_cast<DWORD>(sizeof(SharedGpuFrameHeader)), kGpuMappingName);
    if (!mapping_) return false;
    view_ = static_cast<SharedGpuFrameHeader*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0,
                                                             sizeof(SharedGpuFrameHeader)));
    if (!view_) return false;
    *view_ = SharedGpuFrameHeader{};
    return true;
}

void SharedGpuFrameWriter::Close() {
    if (view_) { UnmapViewOfFile(view_); view_ = nullptr; }
    if (mapping_) { CloseHandle(mapping_); mapping_ = nullptr; }
    if (event_) { CloseHandle(event_); event_ = nullptr; }
    if (mutex_) { CloseHandle(mutex_); mutex_ = nullptr; }
}

bool SharedGpuFrameWriter::Publish(uint64_t sharedHandle, uint32_t width, uint32_t height,
                                   uint64_t frameNo, uint32_t fps) {
    if (!view_ || !sharedHandle || !width || !height) return false;
    if (WaitForSingleObject(mutex_, 10) != WAIT_OBJECT_0) return false;
    view_->magic = kGpuMagic;
    view_->width = width;
    view_->height = height;
    view_->format = 1;
    view_->fpsNumerator = fps;
    view_->fpsDenominator = 1;
    view_->frameNumber = frameNo;
    view_->sharedHandle = sharedHandle;
    LARGE_INTEGER q{};
    QueryPerformanceCounter(&q);
    view_->qpc = q.QuadPart;
    ReleaseMutex(mutex_);
    if (event_) SetEvent(event_);
    return true;
}

bool SharedGpuFrameReader::Open() {
    Close();
    mutex_ = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, kGpuMutexName);
    event_ = OpenEventW(SYNCHRONIZE, FALSE, kGpuEventName);
    mapping_ = OpenFileMappingW(FILE_MAP_READ, FALSE, kGpuMappingName);
    if (!mapping_) return false;
    view_ = static_cast<const SharedGpuFrameHeader*>(MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0,
                                                                  sizeof(SharedGpuFrameHeader)));
    return view_ != nullptr;
}

void SharedGpuFrameReader::Close() {
    if (view_) { UnmapViewOfFile(view_); view_ = nullptr; }
    if (mapping_) { CloseHandle(mapping_); mapping_ = nullptr; }
    if (event_) { CloseHandle(event_); event_ = nullptr; }
    if (mutex_) { CloseHandle(mutex_); mutex_ = nullptr; }
}

bool SharedGpuFrameReader::WaitForFrame(DWORD timeoutMs) {
    return event_ ? WaitForSingleObject(event_, timeoutMs) == WAIT_OBJECT_0 : false;
}

bool SharedGpuFrameReader::Read(SharedGpuFrameHeader& out) {
    if (!view_) return false;
    if (mutex_ && WaitForSingleObject(mutex_, 5) != WAIT_OBJECT_0) return false;
    const SharedGpuFrameHeader tmp = *view_;
    if (mutex_) ReleaseMutex(mutex_);
    if (tmp.magic != kGpuMagic || !tmp.sharedHandle || !tmp.width || !tmp.height) return false;
    out = tmp;
    return true;
}
}
