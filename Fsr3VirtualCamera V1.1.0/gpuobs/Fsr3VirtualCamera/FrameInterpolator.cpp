#include "FrameInterpolator.h"
#include <algorithm>

bool FrameInterpolator::Initialize(uint32_t width, uint32_t height, std::wstring& error) {
    if (!width || !height) {
        error = L"Invalid frame-generation dimensions";
        return false;
    }
    width_ = width;
    height_ = height;
    return true;
}

void FrameInterpolator::Shutdown() {
    width_ = height_ = 0;
}

bool FrameInterpolator::GenerateMidpoint(const std::vector<uint8_t>& previous,
                                         const std::vector<uint8_t>& current,
                                         std::vector<uint8_t>& generated,
                                         std::wstring& error) const {
    const size_t bytes = static_cast<size_t>(width_) * height_ * 4;
    if (!width_ || !height_ || previous.size() != bytes || current.size() != bytes) {
        error = L"Frame interpolation input size mismatch";
        return false;
    }

    generated.resize(bytes);
    // Fast midpoint blend fallback. This is deliberately isolated so it can be
    // replaced with FidelityFX Optical Flow + Frame Generation without touching
    // capture, preview, or OBS transport.
    for (size_t i = 0; i < bytes; i += 4) {
        generated[i + 0] = static_cast<uint8_t>((static_cast<unsigned>(previous[i + 0]) + current[i + 0] + 1) >> 1);
        generated[i + 1] = static_cast<uint8_t>((static_cast<unsigned>(previous[i + 1]) + current[i + 1] + 1) >> 1);
        generated[i + 2] = static_cast<uint8_t>((static_cast<unsigned>(previous[i + 2]) + current[i + 2] + 1) >> 1);
        generated[i + 3] = 255;
    }
    return true;
}
