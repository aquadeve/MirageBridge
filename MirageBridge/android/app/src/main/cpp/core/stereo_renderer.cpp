#include "core/stereo_renderer.h"

#include <android/log.h>
#include <chrono>
#include <cstring>

#include <GLES3/gl3.h>

#define MB_LOG_TAG "MirageBridgeRender"
#define MB_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, MB_LOG_TAG, __VA_ARGS__)

namespace miragebridge {

namespace {
uint64_t MonotonicNs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

constexpr const char* kVertexShader = R"(#version 300 es
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aUv;
out vec2 vUv;
void main() {
    vUv = aUv;
    gl_Position = vec4(aPosition, 0.0, 1.0);
}
)";

constexpr const char* kFragmentShader = R"(#version 300 es
precision mediump float;
uniform sampler2D uFrame;
in vec2 vUv;
out vec4 outColor;
void main() {
    outColor = texture(uFrame, vUv);
}
)";
}

bool StereoRenderer::Initialize() {
    latestClientFrame_ = std::make_unique<SBSFramePacket>();
    drawScratch_ = std::make_unique<SBSFramePacket>();
    return pipeline_.Initialize(width_, height_);
}

void StereoRenderer::Shutdown() {
    pipeline_.Shutdown();
    latestClientFrame_.reset();
    drawScratch_.reset();
    latestClientFrameId_ = UINT64_MAX;
    uploadedClientFrameId_ = UINT64_MAX;
    drawProgram_ = 0;
    drawTexture_ = 0;
    drawVbo_ = 0;
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
    header.targetDisplayNs = header.monotonicNs + 13888888ULL;
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

void StereoRenderer::SubmitClientFrame(const SBSFramePacket& frame) {
    if (frame.header.magic != kProtocolMagic ||
        frame.header.version != kProtocolVersion ||
        frame.header.payloadBytes > kSbsBytes) {
        return;
    }

    std::lock_guard<std::mutex> lock(latestFrameMutex_);
    if (!latestClientFrame_) {
        latestClientFrame_ = std::make_unique<SBSFramePacket>();
    }
    std::memcpy(latestClientFrame_.get(), &frame, sizeof(SBSFramePacket));
    latestClientFrameId_ = frame.header.frameId;
}

uint32_t StereoRenderer::CompileShader(uint32_t type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        char log[512]{};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        MB_LOGE("shader compile failed: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool StereoRenderer::EnsureDrawResources() {
    if (drawProgram_ != 0 && drawTexture_ != 0 && drawVbo_ != 0) {
        return true;
    }

    const GLuint vs = CompileShader(GL_VERTEX_SHADER, kVertexShader);
    const GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFragmentShader);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        char log[512]{};
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        MB_LOGE("program link failed: %s", log);
        glDeleteProgram(program);
        return false;
    }

    const float vertices[] = {
        -1.0f, -1.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 0.0f,
    };
    GLuint texture = 0;
    GLuint vbo = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    drawProgram_ = program;
    drawTexture_ = texture;
    drawVbo_ = vbo;
    return glGetError() == GL_NO_ERROR;
}

void StereoRenderer::DrawLatestFrameToCurrentContext(uint32_t surfaceWidth, uint32_t surfaceHeight) {
    glViewport(0, 0, static_cast<GLsizei>(surfaceWidth), static_cast<GLsizei>(surfaceHeight));
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    if (!EnsureDrawResources()) {
        glClearColor(0.02f, 0.02f, 0.03f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    if (!drawScratch_) {
        drawScratch_ = std::make_unique<SBSFramePacket>();
    }
    auto& frame = *drawScratch_;
    frame.header = {};
    bool haveFrame = false;
    {
        std::lock_guard<std::mutex> lock(latestFrameMutex_);
        haveFrame = latestClientFrame_ && latestClientFrameId_ != UINT64_MAX;
        if (haveFrame && latestClientFrameId_ != uploadedClientFrameId_) {
            std::memcpy(&frame, latestClientFrame_.get(), sizeof(SBSFramePacket));
        }
    }

    if (haveFrame && frame.header.payloadBytes > 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, drawTexture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        if (frame.header.sbsWidth != uploadedWidth_ || frame.header.sbsHeight != uploadedHeight_) {
            glTexImage2D(GL_TEXTURE_2D,
                         0,
                         GL_RGBA,
                         static_cast<GLsizei>(frame.header.sbsWidth),
                         static_cast<GLsizei>(frame.header.sbsHeight),
                         0,
                         GL_RGBA,
                         GL_UNSIGNED_BYTE,
                         frame.payload);
            uploadedWidth_ = frame.header.sbsWidth;
            uploadedHeight_ = frame.header.sbsHeight;
        } else {
            glTexSubImage2D(GL_TEXTURE_2D,
                            0,
                            0,
                            0,
                            static_cast<GLsizei>(frame.header.sbsWidth),
                            static_cast<GLsizei>(frame.header.sbsHeight),
                            GL_RGBA,
                            GL_UNSIGNED_BYTE,
                            frame.payload);
        }
        uploadedClientFrameId_ = frame.header.frameId;
    }

    if (uploadedClientFrameId_ == UINT64_MAX) {
        glClearColor(0.02f, 0.02f, 0.03f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(drawProgram_);
    glUniform1i(glGetUniformLocation(drawProgram_, "uFrame"), 0);
    glBindTexture(GL_TEXTURE_2D, drawTexture_);
    glBindBuffer(GL_ARRAY_BUFFER, drawVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, reinterpret_cast<void*>(sizeof(float) * 2));
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

}
