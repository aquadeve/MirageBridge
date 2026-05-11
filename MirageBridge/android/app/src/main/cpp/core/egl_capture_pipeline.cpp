#include "core/egl_capture_pipeline.h"

#include <GLES3/gl3.h>

namespace miragebridge {

bool EGLCapturePipeline::Initialize(uint32_t width, uint32_t height) {
    width_ = width;
    height_ = height;
    glGenFramebuffers(1, &leftFbo_);
    glGenFramebuffers(1, &rightFbo_);
    glGenFramebuffers(1, &sbsFbo_);
    glGenTextures(1, &leftTex_);
    glGenTextures(1, &rightTex_);
    glGenTextures(1, &sbsTex_);
    return true;
}

void EGLCapturePipeline::Shutdown() {
    if (leftFbo_) glDeleteFramebuffers(1, &leftFbo_);
    if (rightFbo_) glDeleteFramebuffers(1, &rightFbo_);
    if (sbsFbo_) glDeleteFramebuffers(1, &sbsFbo_);
    if (leftTex_) glDeleteTextures(1, &leftTex_);
    if (rightTex_) glDeleteTextures(1, &rightTex_);
    if (sbsTex_) glDeleteTextures(1, &sbsTex_);
    leftFbo_ = 0;
    rightFbo_ = 0;
    sbsFbo_ = 0;
    leftTex_ = 0;
    rightTex_ = 0;
    sbsTex_ = 0;
}

bool EGLCapturePipeline::RenderEyesToSbs(uint64_t frameId) {
    const float r = static_cast<float>((frameId % 180) / 180.0);
    const float b = static_cast<float>(((frameId + 60) % 180) / 180.0);

    glBindFramebuffer(GL_FRAMEBUFFER, leftFbo_);
    glViewport(0, 0, static_cast<GLsizei>(width_ / 2), static_cast<GLsizei>(height_));
    glClearColor(r, 0.2f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glBindFramebuffer(GL_FRAMEBUFFER, rightFbo_);
    glViewport(0, 0, static_cast<GLsizei>(width_ / 2), static_cast<GLsizei>(height_));
    glClearColor(0.2f, 0.2f, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glBindFramebuffer(GL_FRAMEBUFFER, sbsFbo_);
    glViewport(0, 0, static_cast<GLsizei>(width_), static_cast<GLsizei>(height_));
    glClearColor(r, 0.2f, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    return true;
}
bool EGLCapturePipeline::ReadSbsPixels(uint8_t* dst, size_t dstSize) {
    if (!dst) {
        return false;
    }
    const size_t required = static_cast<size_t>(width_) * static_cast<size_t>(height_) * 4;
    if (dstSize < required) {
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, sbsFbo_);
    glReadPixels(0, 0, static_cast<GLsizei>(width_), static_cast<GLsizei>(height_), GL_RGBA, GL_UNSIGNED_BYTE, dst);
    return true;
}

}
