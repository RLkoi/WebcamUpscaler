#pragma once
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <string>
#include <vector>

struct CameraInfo { std::wstring name; std::wstring symbolicLink; };
class CameraCapture {
public:
    static std::vector<CameraInfo> Enumerate();
    bool Open(const CameraInfo& info, std::wstring& error);
    bool ReadFrame(std::vector<uint8_t>& bgra, uint32_t& w, uint32_t& h, uint32_t& stride, std::wstring& error);
    void Close();
private:
    Microsoft::WRL::ComPtr<IMFSourceReader> reader_;
    uint32_t w_=0,h_=0;
};
