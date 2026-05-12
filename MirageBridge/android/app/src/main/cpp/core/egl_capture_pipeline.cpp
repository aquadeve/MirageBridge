#include "core/egl_capture_pipeline.h"

#include <android/log.h>
#include <GLES3/gl3.h>

#define MB_LOG_TAG "MirageBridgeEGL"
#define MB_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, MB_LOG_TAG, __VA_ARGS__)

#ifndef EGL_OPENGL_ES3_BIT
#define EGL_OPENGL_ES3_BIT 0x00000040
#endif

namespace miragebridge {

namespace {
bool CheckFramebuffer(const char* name) {
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        MB_LOGE("%s framebuffer incomplete status=0x%x", name, status);
        return false;
    }
    return true;
}
}

bool EGLCapturePipeline::Initialize(uint32_t width, uint32_t height) {
    width_ = width;
    height_ = height;

    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display_ == EGL_NO_DISPLAY || !eglInitialize(display_, nullptr, nullptr)) {
        MB_LOGE("eglInitialize failed");
        return false;
    }

    const EGLint configAttribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 16,
        EGL_NONE,
    };

    EGLConfig config = nullptr;
    EGLint configCount = 0;
    if (!eglChooseConfig(display_, configAttribs, &config, 1, &configCount) || configCount == 0) {
        MB_LOGE("eglChooseConfig failed");
        Shutdown();
        return false;
    }

    const EGLint surfaceAttribs[] = {
        EGL_WIDTH, 1,
        EGL_HEIGHT, 1,
        EGL_NONE,
    };
    surface_ = eglCreatePbufferSurface(display_, config, surfaceAttribs);
    if (surface_ == EGL_NO_SURFACE) {
        MB_LOGE("eglCreatePbufferSurface failed");
        Shutdown();
        return false;
    }

    const EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE,
    };
    context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, contextAttribs);
    if (context_ == EGL_NO_CONTEXT) {
        MB_LOGE("eglCreateContext failed");
        Shutdown();
        return false;
    }
    if (!MakeCurrent()) {
        Shutdown();
        return false;
    }

    glGenFramebuffers(1, &leftFbo_);
    glGenFramebuffers(1, &rightFbo_);
    glGenFramebuffers(1, &sbsFbo_);

    if (!CreateTexture(width_ / 2, height_, &leftTex_) ||
        !CreateTexture(width_ / 2, height_, &rightTex_) ||
        !CreateTexture(width_, height_, &sbsTex_) ||
        !AttachFramebuffer(leftFbo_, leftTex_) ||
        !CheckFramebuffer("left") ||
        !AttachFramebuffer(rightFbo_, rightTex_) ||
        !CheckFramebuffer("right") ||
        !AttachFramebuffer(sbsFbo_, sbsTex_) ||
        !CheckFramebuffer("sbs")) {
        Shutdown();
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    return true;
}

bool EGLCapturePipeline::MakeCurrent() {
    return display_ != EGL_NO_DISPLAY &&
           context_ != EGL_NO_CONTEXT &&
           surface_ != EGL_NO_SURFACE &&
           eglMakeCurrent(display_, surface_, surface_, context_);
}

bool EGLCapturePipeline::CreateTexture(uint32_t width, uint32_t height, uint32_t* texture) {
    if (!texture) {
        return false;
    }
    glGenTextures(1, texture);
    glBindTexture(GL_TEXTURE_2D, *texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA8,
                 static_cast<GLsizei>(width),
                 static_cast<GLsizei>(height),
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 nullptr);
    return glGetError() == GL_NO_ERROR;
}

bool EGLCapturePipeline::AttachFramebuffer(uint32_t framebuffer, uint32_t texture) {
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    const GLenum buffers[] = {GL_COLOR_ATTACHMENT0};
    glDrawBuffers(1, buffers);
    return glGetError() == GL_NO_ERROR;
}

void EGLCapturePipeline::Shutdown() {
    if (display_ != EGL_NO_DISPLAY && context_ != EGL_NO_CONTEXT) {
        MakeCurrent();
    }
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

    if (display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (context_ != EGL_NO_CONTEXT) {
            eglDestroyContext(display_, context_);
        }
        if (surface_ != EGL_NO_SURFACE) {
            eglDestroySurface(display_, surface_);
        }
        eglTerminate(display_);
    }

    display_ = EGL_NO_DISPLAY;
    context_ = EGL_NO_CONTEXT;
    surface_ = EGL_NO_SURFACE;
}

bool EGLCapturePipeline::RenderEyesToSbs(uint64_t frameId) {
    if (!MakeCurrent()) {
        return false;
    }

    const float phase = static_cast<float>((frameId % 144) / 144.0);
    const float r = 0.15f + phase * 0.65f;
    const float b = 0.80f - phase * 0.45f;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);

    glBindFramebuffer(GL_FRAMEBUFFER, leftFbo_);
    glViewport(0, 0, static_cast<GLsizei>(width_ / 2), static_cast<GLsizei>(height_));
    glClearColor(r, 0.18f, 0.22f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glBindFramebuffer(GL_FRAMEBUFFER, rightFbo_);
    glViewport(0, 0, static_cast<GLsizei>(width_ / 2), static_cast<GLsizei>(height_));
    glClearColor(0.16f, 0.24f, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, leftFbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sbsFbo_);
    glBlitFramebuffer(0,
                      0,
                      static_cast<GLint>(width_ / 2),
                      static_cast<GLint>(height_),
                      0,
                      0,
                      static_cast<GLint>(width_ / 2),
                      static_cast<GLint>(height_),
                      GL_COLOR_BUFFER_BIT,
                      GL_NEAREST);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, rightFbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sbsFbo_);
    glBlitFramebuffer(0,
                      0,
                      static_cast<GLint>(width_ / 2),
                      static_cast<GLint>(height_),
                      static_cast<GLint>(width_ / 2),
                      0,
                      static_cast<GLint>(width_),
                      static_cast<GLint>(height_),
                      GL_COLOR_BUFFER_BIT,
                      GL_NEAREST);
    glFlush();
    return glGetError() == GL_NO_ERROR;
}

bool EGLCapturePipeline::ReadSbsPixels(uint8_t* dst, size_t dstSize) {
    if (!dst || !MakeCurrent()) {
        return false;
    }
    const size_t required = static_cast<size_t>(width_) * static_cast<size_t>(height_) * 4;
    if (dstSize < required) {
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, sbsFbo_);
    glReadPixels(0, 0, static_cast<GLsizei>(width_), static_cast<GLsizei>(height_), GL_RGBA, GL_UNSIGNED_BYTE, dst);
    const bool ok = glGetError() == GL_NO_ERROR;
    eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    return ok;
}

}
