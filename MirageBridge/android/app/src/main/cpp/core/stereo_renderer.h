#pragma once

#include <cstdint>
#include <memory>
#include <mutex>

#include "miragebridge_protocol.h"

#include "core/egl_capture_pipeline.h"

namespace miragebridge {

class StereoRenderer {
public:
    bool Initialize();
    void Shutdown();
    bool RenderAndPack(uint64_t frameId, SBSFramePacket* outFrame);
    void SubmitClientFrame(const SBSFramePacket& frame);
    void DrawLatestFrameToCurrentContext(uint32_t surfaceWidth, uint32_t surfaceHeight);

private:
    bool EnsureDrawResources();
    uint32_t CompileShader(uint32_t type, const char* source);

    uint32_t width_ = 2048;
    uint32_t height_ = 1024;
    EGLCapturePipeline pipeline_;
    std::mutex latestFrameMutex_;
    std::unique_ptr<SBSFramePacket> latestClientFrame_;
    std::unique_ptr<SBSFramePacket> drawScratch_;
    uint64_t latestClientFrameId_ = UINT64_MAX;
    uint64_t uploadedClientFrameId_ = UINT64_MAX;
    uint32_t uploadedWidth_ = 0;
    uint32_t uploadedHeight_ = 0;
    uint32_t drawProgram_ = 0;
    uint32_t drawTexture_ = 0;
    uint32_t drawVbo_ = 0;
};

}
