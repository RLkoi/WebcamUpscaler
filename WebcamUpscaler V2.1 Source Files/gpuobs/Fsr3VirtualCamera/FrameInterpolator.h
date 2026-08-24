#pragma once
#include <cstdint>
#include <vector>
#include <string>

// Camera-video frame interpolation fallback.
// This is intentionally NOT labelled as AMD FSR Frame Generation: genuine FSR
// FG needs renderer motion/depth inputs. This midpoint interpolator gives the
// app a useful 2x output mode for camera video while keeping the stage isolated
// for a future FidelityFX optical-flow/frame-generation implementation.
class FrameInterpolator {
public:
    bool Initialize(uint32_t width, uint32_t height, std::wstring& error);
    void Shutdown();

    // Produces a midpoint frame between previous and current BGRA images.
    bool GenerateMidpoint(const std::vector<uint8_t>& previous,
                          const std::vector<uint8_t>& current,
                          std::vector<uint8_t>& generated,
                          std::wstring& error) const;
private:
    uint32_t width_ = 0;
    uint32_t height_ = 0;
};
