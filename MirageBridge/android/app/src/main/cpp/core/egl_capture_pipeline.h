#pragma once

#include <cstddef>
#include <cstdint>

namespace miragebridge {

class EGLCapturePipeline {
public:
    bool Initialize(uint32_t width, uint32_t height);
    void Shutdown();
    bool RenderEyesToSbs(uint64_t frameId);
    bool ReadSbsPixels(uint8_t* dst, size_t dstSize);

private:
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t leftFbo_ = 0;
    uint32_t rightFbo_ = 0;
    uint32_t sbsFbo_ = 0;
    uint32_t leftTex_ = 0;
    uint32_t rightTex_ = 0;
    uint32_t sbsTex_ = 0;
};

}
