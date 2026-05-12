#pragma once

#include <cstddef>
#include <cstdint>

#include <EGL/egl.h>

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
    bool CreateTexture(uint32_t width, uint32_t height, uint32_t* texture);
    bool AttachFramebuffer(uint32_t framebuffer, uint32_t texture);
    bool MakeCurrent();

    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLSurface surface_ = EGL_NO_SURFACE;
    uint32_t leftFbo_ = 0;
    uint32_t rightFbo_ = 0;
    uint32_t sbsFbo_ = 0;
    uint32_t leftTex_ = 0;
    uint32_t rightTex_ = 0;
    uint32_t sbsTex_ = 0;
};

}
