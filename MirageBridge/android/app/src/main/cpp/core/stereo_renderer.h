#pragma once

#include <cstdint>

#include "../../../../common/miragebridge_protocol.h"

#include "core/egl_capture_pipeline.h"

namespace miragebridge {

class StereoRenderer {
public:
    bool Initialize();
    void Shutdown();
    bool RenderAndPack(uint64_t frameId, SBSFramePacket* outFrame);

private:
    uint32_t width_ = 2048;
    uint32_t height_ = 1024;
    EGLCapturePipeline pipeline_;
};

}
