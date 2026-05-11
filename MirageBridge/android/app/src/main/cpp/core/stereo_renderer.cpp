#include "core/stereo_renderer.h"

#include <chrono>

namespace miragebridge {

namespace {
uint64_t MonotonicNs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
}

bool StereoRenderer::Initialize() {
    return pipeline_.Initialize(width_, height_);
}

void StereoRenderer::Shutdown() {
    pipeline_.Shutdown();
}

bool StereoRenderer::RenderAndPack(uint64_t frameId, SBSFramePacket* outFrame) {
    if (!outFrame) {
        return false;
    }

    pipeline_.RenderEyesToSbs(frameId);

    SBSFrameHeader& header = outFrame->header;
    header.magic = kProtocolMagic;
    header.version = kProtocolVersion;
    header.frameId = frameId;
    header.monotonicNs = MonotonicNs();
    header.sbsWidth = width_;
    header.sbsHeight = height_;
    header.strideBytes = width_ * 4;
    header.format = 1;
    header.payloadBytes = header.strideBytes * height_;

    const uint8_t left = static_cast<uint8_t>(frameId & 0xFF);
    const uint8_t right = static_cast<uint8_t>((frameId + 64) & 0xFF);
    if (!pipeline_.ReadSbsPixels(outFrame->payload, sizeof(outFrame->payload))) {
        const uint32_t half = width_ / 2;
        for (uint32_t y = 0; y < height_; ++y) {
            for (uint32_t x = 0; x < width_; ++x) {
                const size_t idx = static_cast<size_t>(y) * header.strideBytes + static_cast<size_t>(x) * 4;
                const bool isLeft = x < half;
                outFrame->payload[idx + 0] = isLeft ? left : 16;
                outFrame->payload[idx + 1] = 32;
                outFrame->payload[idx + 2] = isLeft ? 16 : right;
                outFrame->payload[idx + 3] = 255;
            }
        }
    }
    return true;
}

}
