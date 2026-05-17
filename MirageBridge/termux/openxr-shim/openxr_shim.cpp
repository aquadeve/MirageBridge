#include "openxr_shim.h"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <cmath>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "miragebridge_protocol.h"
#include "transport_reader.h"
#include "transport_writer.h"

namespace {

constexpr XrSystemId kSystemId = 1;
constexpr uint32_t kViewCount = 2;
constexpr uint32_t kSwapchainImageCount = 3;
constexpr XrDuration kDisplayPeriodNs = 13888888;
constexpr XrVersion kRuntimeApiVersion = XR_MAKE_VERSION(1, 0, 0);

struct Instance {
    uint64_t id = 0;
    std::vector<std::string> enabledExtensions;
};

struct Session {
    uint64_t id = 0;
    Instance* instance = nullptr;
    XrSessionState state = XR_SESSION_STATE_IDLE;
    bool running = false;
    bool usesOpenGL = false;
    bool usesOpenGLES = false;
    uint64_t frameId = 0;
    XrTime predictedDisplayTime = 0;
    XrDuration predictedDisplayPeriod = kDisplayPeriodNs;
    bool frameWaited = false;
    bool frameBegun = false;
};

struct Space {
    uint64_t id = 0;
    Session* session = nullptr;
    XrReferenceSpaceType type = XR_REFERENCE_SPACE_TYPE_LOCAL;
    XrPosef pose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
};

struct Swapchain {
    uint64_t id = 0;
    Session* session = nullptr;
    XrSwapchainCreateInfo createInfo{};
    uint32_t acquiredIndex = 0;
    uint32_t releasedIndex = 0;
    bool imageAcquired = false;
    std::vector<uint32_t> glImages;
};

std::mutex g_mutex;
uint64_t g_nextId = 1;
std::vector<Instance*> g_instances;
std::vector<Session*> g_sessions;
std::vector<Space*> g_spaces;
std::vector<Swapchain*> g_swapchains;
std::vector<XrEventDataSessionStateChanged> g_events;
miragebridge::RingReader g_trackingReader;
bool g_trackingReaderInit = false;
miragebridge::RingWriter g_clientFrameWriter;
bool g_clientFrameWriterInit = false;
std::mutex g_submitMutex;
std::unique_ptr<miragebridge::SBSFramePacket> g_submitScratch;
miragebridge::XRPacket g_lastPose{};
uint64_t g_lastPoseSeq = 0;
bool g_poseFilterInit = false;
uint64_t g_poseFilterSeq = UINT64_MAX;
float g_filteredAngularVel[3] = {};
float g_filteredLinearVel[3] = {};
uint64_t g_nextPath = 1;
std::unordered_map<std::string, XrPath> g_paths;
std::unordered_map<XrPath, std::string> g_pathNames;

void RefreshPose();
Swapchain* GetSwapchain(XrSwapchain handle);

uint64_t MonotonicNs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

void Logf(const char* fmt, ...) {
    std::fprintf(stderr, "[MirageOpenXR] ");
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);
    std::fprintf(stderr, "\n");
}

template <typename T>
T* FromHandle(T* handle) {
    return handle;
}

XrInstance ToHandle(Instance* instance) {
    return reinterpret_cast<XrInstance>(instance);
}

XrSession ToHandle(Session* session) {
    return reinterpret_cast<XrSession>(session);
}

XrSpace ToHandle(Space* space) {
    return reinterpret_cast<XrSpace>(space);
}

XrSwapchain ToHandle(Swapchain* swapchain) {
    return reinterpret_cast<XrSwapchain>(swapchain);
}

bool HasExtension(const Instance* instance, const char* extension) {
    if (!instance || !extension) {
        return false;
    }
    return std::find(instance->enabledExtensions.begin(), instance->enabledExtensions.end(), extension) != instance->enabledExtensions.end();
}

void CopyString(char* dst, size_t dstSize, const char* src) {
    if (!dst || dstSize == 0) {
        return;
    }
    std::snprintf(dst, dstSize, "%s", src ? src : "");
}

template <typename Writer>
bool CopyCounted(uint32_t capacity, uint32_t* outCount, uint32_t available, Writer writeOne) {
    if (!outCount) {
        return false;
    }
    *outCount = available;
    if (capacity == 0) {
        return true;
    }
    if (capacity < available) {
        return false;
    }
    for (uint32_t i = 0; i < available; ++i) {
        writeOne(i);
    }
    return true;
}

float ClampFloat(float value, float lo, float hi) {
    return std::max(lo, std::min(hi, value));
}

XrQuaternionf NormalizeQuat(XrQuaternionf q) {
    const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (len <= 0.0f || !std::isfinite(len)) {
        return {0.0f, 0.0f, 0.0f, 1.0f};
    }
    q.x /= len;
    q.y /= len;
    q.z /= len;
    q.w /= len;
    return q;
}

XrQuaternionf MulQuat(const XrQuaternionf& a, const XrQuaternionf& b) {
    return NormalizeQuat({
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    });
}

XrVector3f RotateVector(const XrQuaternionf& q, const XrVector3f& v) {
    const XrVector3f u{q.x, q.y, q.z};
    const XrVector3f uv{
        u.y * v.z - u.z * v.y,
        u.z * v.x - u.x * v.z,
        u.x * v.y - u.y * v.x,
    };
    const XrVector3f uuv{
        u.y * uv.z - u.z * uv.y,
        u.z * uv.x - u.x * uv.z,
        u.x * uv.y - u.y * uv.x,
    };
    return {
        v.x + 2.0f * (q.w * uv.x + uuv.x),
        v.y + 2.0f * (q.w * uv.y + uuv.y),
        v.z + 2.0f * (q.w * uv.z + uuv.z),
    };
}

XrQuaternionf IntegrateAngularVelocity(const XrQuaternionf& base, const float angularVel[3], float dt) {
    const float wx = angularVel[0];
    const float wy = angularVel[1];
    const float wz = angularVel[2];
    const float speed = std::sqrt(wx * wx + wy * wy + wz * wz);
    if (speed < 0.00001f || dt <= 0.0f) {
        return NormalizeQuat(base);
    }

    const float angle = ClampFloat(speed * dt, -0.35f, 0.35f);
    const float half = angle * 0.5f;
    const float s = std::sin(half) / speed;
    const XrQuaternionf delta{wx * s, wy * s, wz * s, std::cos(half)};
    return MulQuat(delta, base);
}

void UpdatePoseFilter() {
    if (g_poseFilterSeq == g_lastPoseSeq && g_poseFilterInit) {
        return;
    }
    constexpr float alpha = 0.35f;
    if (!g_poseFilterInit) {
        std::memcpy(g_filteredAngularVel, g_lastPose.angularVel, sizeof(g_filteredAngularVel));
        std::memcpy(g_filteredLinearVel, g_lastPose.linearVel, sizeof(g_filteredLinearVel));
        g_poseFilterInit = true;
    } else {
        for (int i = 0; i < 3; ++i) {
            g_filteredAngularVel[i] = g_filteredAngularVel[i] * (1.0f - alpha) + g_lastPose.angularVel[i] * alpha;
            g_filteredLinearVel[i] = g_filteredLinearVel[i] * (1.0f - alpha) + g_lastPose.linearVel[i] * alpha;
        }
    }
    g_poseFilterSeq = g_lastPoseSeq;
}

XrPosef PredictHeadPose(XrTime displayTime) {
    RefreshPose();
    UpdatePoseFilter();

    XrPosef out{};
    out.orientation = NormalizeQuat({g_lastPose.rot[0], g_lastPose.rot[1], g_lastPose.rot[2], g_lastPose.rot[3]});
    out.position = {g_lastPose.pos[0], g_lastPose.pos[1], g_lastPose.pos[2]};

    const uint64_t sampleNs = g_lastPose.predictedDisplayNs ? g_lastPose.predictedDisplayNs : g_lastPose.monotonicNs;
    const uint64_t targetNs = displayTime > 0
        ? static_cast<uint64_t>(displayTime)
        : (g_lastPose.predictedDisplayNs ? g_lastPose.predictedDisplayNs : MonotonicNs() + kDisplayPeriodNs);
    float dt = static_cast<float>((static_cast<int64_t>(targetNs) - static_cast<int64_t>(sampleNs)) * 1e-9);
    dt = ClampFloat(dt, -0.01f, 0.05f);

    out.orientation = IntegrateAngularVelocity(out.orientation, g_filteredAngularVel, dt);
    for (int i = 0; i < 3; ++i) {
        (&out.position.x)[i] += g_filteredLinearVel[i] * dt;
    }
    return out;
}

void PushSessionState(Session* session, XrSessionState state) {
    if (!session) {
        return;
    }
    session->state = state;
    XrEventDataSessionStateChanged event{};
    event.type = XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED;
    event.session = ToHandle(session);
    event.state = state;
    event.time = static_cast<XrTime>(MonotonicNs());
    g_events.push_back(event);
    Logf("queue session state %d session=%p", state, static_cast<void*>(session));
}

void EnsureTrackingReader() {
    if (g_trackingReaderInit) {
        return;
    }
    const auto cfg = miragebridge::DefaultConfig();
    g_trackingReader.Open(cfg.trackingName, sizeof(miragebridge::XRPacket));
    g_trackingReaderInit = true;
}

void RefreshPose() {
    EnsureTrackingReader();
    miragebridge::XRPacket packet{};
    uint64_t seq = 0;
    if (g_trackingReader.ReadLatest(&packet, sizeof(packet), &seq) && packet.magic == miragebridge::kProtocolMagic) {
        g_lastPose = packet;
        g_lastPoseSeq = seq;
        return;
    }

    if (g_lastPose.magic != miragebridge::kProtocolMagic) {
        g_lastPose.magic = miragebridge::kProtocolMagic;
        g_lastPose.version = miragebridge::kProtocolVersion;
        g_lastPose.monotonicNs = MonotonicNs();
        g_lastPose.predictedDisplayNs = g_lastPose.monotonicNs + kDisplayPeriodNs;
        g_lastPose.rot[3] = 1.0f;
        g_lastPose.pos[1] = 1.65f;
        g_lastPose.displayWidth = 2560;
        g_lastPose.displayHeight = 1440;
        g_lastPose.displayHz = 72;
        g_lastPose.trackingHz = 200;
        for (uint32_t eye = 0; eye < kViewCount; ++eye) {
            for (int i = 0; i < 16; ++i) {
                g_lastPose.eyes[eye].view[i] = (i % 5 == 0) ? 1.0f : 0.0f;
                g_lastPose.eyes[eye].proj[i] = (i % 5 == 0) ? 1.0f : 0.0f;
            }
            g_lastPose.eyes[eye].view[3] = eye == 0 ? -0.032f : 0.032f;
            g_lastPose.eyes[eye].fov[0] = -0.75f;
            g_lastPose.eyes[eye].fov[1] = 0.75f;
            g_lastPose.eyes[eye].fov[2] = 0.75f;
            g_lastPose.eyes[eye].fov[3] = -0.75f;
        }
    }
}

void FillPoseFromPacket(uint32_t eye, XrTime displayTime, XrPosef* pose, XrFovf* fov) {
    *pose = PredictHeadPose(displayTime);
    const float eyeOffset = g_lastPose.eyes[eye].view[3] != 0.0f ? g_lastPose.eyes[eye].view[3] : (eye == 0 ? -0.032f : 0.032f);
    const XrVector3f rotatedEye = RotateVector(pose->orientation, {eyeOffset, 0.0f, 0.0f});
    pose->position.x += rotatedEye.x;
    pose->position.y += rotatedEye.y;
    pose->position.z += rotatedEye.z;
    fov->angleLeft = g_lastPose.eyes[eye].fov[0] != 0.0f ? g_lastPose.eyes[eye].fov[0] : -0.75f;
    fov->angleRight = g_lastPose.eyes[eye].fov[1] != 0.0f ? g_lastPose.eyes[eye].fov[1] : 0.75f;
    fov->angleUp = g_lastPose.eyes[eye].fov[2] != 0.0f ? g_lastPose.eyes[eye].fov[2] : 0.75f;
    fov->angleDown = g_lastPose.eyes[eye].fov[3] != 0.0f ? g_lastPose.eyes[eye].fov[3] : -0.75f;
}

using PFN_glGenTextures = void (*)(int, uint32_t*);
using PFN_glBindTexture = void (*)(uint32_t, uint32_t);
using PFN_glTexParameteri = void (*)(uint32_t, uint32_t, int);
using PFN_glTexImage2D = void (*)(uint32_t, int, int, int, int, int, uint32_t, uint32_t, const void*);
using PFN_glGenFramebuffers = void (*)(int, uint32_t*);
using PFN_glBindFramebuffer = void (*)(uint32_t, uint32_t);
using PFN_glFramebufferTexture2D = void (*)(uint32_t, uint32_t, uint32_t, uint32_t, int);
using PFN_glCheckFramebufferStatus = uint32_t (*)(uint32_t);
using PFN_glReadPixels = void (*)(int, int, int, int, uint32_t, uint32_t, void*);
using PFN_glDeleteFramebuffers = void (*)(int, const uint32_t*);
using PFN_glPixelStorei = void (*)(uint32_t, int);

constexpr uint32_t GL_TEXTURE_2D_MB = 0x0DE1;
constexpr uint32_t GL_FRAMEBUFFER_MB = 0x8D40;
constexpr uint32_t GL_COLOR_ATTACHMENT0_MB = 0x8CE0;
constexpr uint32_t GL_FRAMEBUFFER_COMPLETE_MB = 0x8CD5;
constexpr uint32_t GL_TEXTURE_MIN_FILTER_MB = 0x2801;
constexpr uint32_t GL_TEXTURE_MAG_FILTER_MB = 0x2800;
constexpr uint32_t GL_TEXTURE_WRAP_S_MB = 0x2802;
constexpr uint32_t GL_TEXTURE_WRAP_T_MB = 0x2803;
constexpr uint32_t GL_LINEAR_MB = 0x2601;
constexpr uint32_t GL_CLAMP_TO_EDGE_MB = 0x812F;
constexpr uint32_t GL_RGBA_MB = 0x1908;
constexpr uint32_t GL_UNSIGNED_BYTE_MB = 0x1401;
constexpr uint32_t GL_UNPACK_ALIGNMENT_MB = 0x0CF5;
constexpr uint32_t GL_PACK_ALIGNMENT_MB = 0x0D05;
constexpr int GL_RGBA8_MB = 0x8058;
constexpr int GL_SRGB8_ALPHA8_MB = 0x8C43;

bool CreateOpenGLTexture(uint32_t width, uint32_t height, int64_t format, uint32_t* outTexture) {
    auto genTextures = reinterpret_cast<PFN_glGenTextures>(dlsym(RTLD_DEFAULT, "glGenTextures"));
    auto bindTexture = reinterpret_cast<PFN_glBindTexture>(dlsym(RTLD_DEFAULT, "glBindTexture"));
    auto texParameteri = reinterpret_cast<PFN_glTexParameteri>(dlsym(RTLD_DEFAULT, "glTexParameteri"));
    auto texImage2D = reinterpret_cast<PFN_glTexImage2D>(dlsym(RTLD_DEFAULT, "glTexImage2D"));
    if (!genTextures || !bindTexture || !texParameteri || !texImage2D) {
        static uint32_t fakeTexture = 1000;
        *outTexture = fakeTexture++;
        Logf("OpenGL texture functions unavailable, issuing placeholder texture=%u", *outTexture);
        return false;
    }

    uint32_t tex = 0;
    genTextures(1, &tex);
    bindTexture(GL_TEXTURE_2D_MB, tex);
    texParameteri(GL_TEXTURE_2D_MB, GL_TEXTURE_MIN_FILTER_MB, GL_LINEAR_MB);
    texParameteri(GL_TEXTURE_2D_MB, GL_TEXTURE_MAG_FILTER_MB, GL_LINEAR_MB);
    texParameteri(GL_TEXTURE_2D_MB, GL_TEXTURE_WRAP_S_MB, GL_CLAMP_TO_EDGE_MB);
    texParameteri(GL_TEXTURE_2D_MB, GL_TEXTURE_WRAP_T_MB, GL_CLAMP_TO_EDGE_MB);
    const int internalFormat = static_cast<int>(format == GL_SRGB8_ALPHA8_MB ? GL_SRGB8_ALPHA8_MB : GL_RGBA8_MB);
    texImage2D(GL_TEXTURE_2D_MB, 0, internalFormat, static_cast<int>(width), static_cast<int>(height), 0, GL_RGBA_MB, GL_UNSIGNED_BYTE_MB, nullptr);
    *outTexture = tex;
    return tex != 0;
}

bool EnsureClientFrameWriter() {
    std::lock_guard<std::mutex> lock(g_submitMutex);
    if (g_clientFrameWriterInit) {
        return true;
    }
    const auto cfg = miragebridge::DefaultConfig();
    g_clientFrameWriterInit = g_clientFrameWriter.Create(cfg.clientFrameName, cfg.clientFrameSlots, sizeof(miragebridge::SBSFramePacket));
    if (!g_submitScratch) {
        g_submitScratch = std::make_unique<miragebridge::SBSFramePacket>();
    }
    if (!g_clientFrameWriterInit) {
        Logf("client frame ring unavailable: %s", cfg.clientFrameName);
    }
    return g_clientFrameWriterInit;
}

void FillFallbackEye(uint32_t eye, miragebridge::SBSFramePacket* frame) {
    if (!frame) {
        return;
    }
    const uint32_t halfWidth = frame->header.sbsWidth / 2;
    const uint32_t xBase = eye == 0 ? 0 : halfWidth;
    const uint8_t r = eye == 0 ? 32 : 8;
    const uint8_t b = eye == 0 ? 8 : 32;
    for (uint32_t y = 0; y < frame->header.sbsHeight; ++y) {
        for (uint32_t x = 0; x < halfWidth; ++x) {
            const size_t idx = static_cast<size_t>(y) * frame->header.strideBytes + static_cast<size_t>(xBase + x) * 4;
            frame->payload[idx + 0] = static_cast<uint8_t>(r + ((x + y) & 0x1f));
            frame->payload[idx + 1] = 16;
            frame->payload[idx + 2] = static_cast<uint8_t>(b + ((x + y) & 0x1f));
            frame->payload[idx + 3] = 255;
        }
    }
}

bool ReadTextureRectToSbs(uint32_t texture,
                          const XrRect2Di& rect,
                          uint32_t eye,
                          miragebridge::SBSFramePacket* frame) {
    if (!texture || !frame || rect.extent.width <= 0 || rect.extent.height <= 0) {
        return false;
    }

    auto genFramebuffers = reinterpret_cast<PFN_glGenFramebuffers>(dlsym(RTLD_DEFAULT, "glGenFramebuffers"));
    auto bindFramebuffer = reinterpret_cast<PFN_glBindFramebuffer>(dlsym(RTLD_DEFAULT, "glBindFramebuffer"));
    auto framebufferTexture2D = reinterpret_cast<PFN_glFramebufferTexture2D>(dlsym(RTLD_DEFAULT, "glFramebufferTexture2D"));
    auto checkFramebufferStatus = reinterpret_cast<PFN_glCheckFramebufferStatus>(dlsym(RTLD_DEFAULT, "glCheckFramebufferStatus"));
    auto readPixels = reinterpret_cast<PFN_glReadPixels>(dlsym(RTLD_DEFAULT, "glReadPixels"));
    auto deleteFramebuffers = reinterpret_cast<PFN_glDeleteFramebuffers>(dlsym(RTLD_DEFAULT, "glDeleteFramebuffers"));
    auto pixelStorei = reinterpret_cast<PFN_glPixelStorei>(dlsym(RTLD_DEFAULT, "glPixelStorei"));
    if (!genFramebuffers || !bindFramebuffer || !framebufferTexture2D || !checkFramebufferStatus || !readPixels || !deleteFramebuffers) {
        return false;
    }

    uint32_t fbo = 0;
    genFramebuffers(1, &fbo);
    bindFramebuffer(GL_FRAMEBUFFER_MB, fbo);
    framebufferTexture2D(GL_FRAMEBUFFER_MB, GL_COLOR_ATTACHMENT0_MB, GL_TEXTURE_2D_MB, texture, 0);
    if (checkFramebufferStatus(GL_FRAMEBUFFER_MB) != GL_FRAMEBUFFER_COMPLETE_MB) {
        deleteFramebuffers(1, &fbo);
        return false;
    }

    if (pixelStorei) {
        pixelStorei(GL_PACK_ALIGNMENT_MB, 1);
    }

    const uint32_t srcWidth = static_cast<uint32_t>(rect.extent.width);
    const uint32_t srcHeight = static_cast<uint32_t>(rect.extent.height);
    std::vector<uint8_t> pixels(static_cast<size_t>(srcWidth) * srcHeight * 4);
    readPixels(rect.offset.x,
               rect.offset.y,
               rect.extent.width,
               rect.extent.height,
               GL_RGBA_MB,
               GL_UNSIGNED_BYTE_MB,
               pixels.data());
    deleteFramebuffers(1, &fbo);

    const uint32_t dstHalfWidth = frame->header.sbsWidth / 2;
    const uint32_t dstWidth = std::min(dstHalfWidth, srcWidth);
    const uint32_t dstHeight = std::min(frame->header.sbsHeight, srcHeight);
    const uint32_t xBase = eye == 0 ? 0 : dstHalfWidth;
    for (uint32_t y = 0; y < dstHeight; ++y) {
        const uint32_t srcY = srcHeight - y - 1;
        const auto* src = pixels.data() + (static_cast<size_t>(srcY) * srcWidth * 4);
        auto* dst = frame->payload + static_cast<size_t>(y) * frame->header.strideBytes + static_cast<size_t>(xBase) * 4;
        std::memcpy(dst, src, static_cast<size_t>(dstWidth) * 4);
    }
    return true;
}

bool SubmitProjectionLayerToBridge(Session* session, const XrFrameEndInfo* frameEndInfo) {
    if (!session || !frameEndInfo || !EnsureClientFrameWriter()) {
        return false;
    }

    if (frameEndInfo->layerCount > 0 && !frameEndInfo->layers) {
        return false;
    }

    const XrCompositionLayerProjection* projection = nullptr;
    for (uint32_t i = 0; i < frameEndInfo->layerCount; ++i) {
        const auto* layer = reinterpret_cast<const XrCompositionLayerBaseHeader*>(frameEndInfo->layers[i]);
        if (layer && layer->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
            projection = reinterpret_cast<const XrCompositionLayerProjection*>(layer);
            break;
        }
    }
    if (!projection || projection->viewCount == 0 || !projection->views) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_submitMutex);
    auto& packet = *g_submitScratch;
    std::memset(&packet, 0, sizeof(packet));
    packet.header.magic = miragebridge::kProtocolMagic;
    packet.header.version = miragebridge::kProtocolVersion;
    packet.header.frameId = session->frameId;
    packet.header.monotonicNs = MonotonicNs();
    packet.header.targetDisplayNs = static_cast<uint64_t>(frameEndInfo->displayTime > 0 ? frameEndInfo->displayTime : session->predictedDisplayTime);
    packet.header.sbsWidth = miragebridge::kSbsWidth;
    packet.header.sbsHeight = miragebridge::kSbsHeight;
    packet.header.strideBytes = miragebridge::kSbsWidth * 4;
    packet.header.format = miragebridge::kBufferPixelFormatRgba8;
    packet.header.payloadBytes = miragebridge::kSbsBytes;

    bool copiedAnyEye = false;
    const uint32_t eyeCount = std::min<uint32_t>(projection->viewCount, kViewCount);
    for (uint32_t eye = 0; eye < eyeCount; ++eye) {
        auto* swapchain = GetSwapchain(projection->views[eye].subImage.swapchain);
        bool copied = false;
        if (swapchain && swapchain->releasedIndex < swapchain->glImages.size()) {
            copied = ReadTextureRectToSbs(swapchain->glImages[swapchain->releasedIndex],
                                          projection->views[eye].subImage.imageRect,
                                          eye,
                                          &packet);
        }
        if (!copied) {
            FillFallbackEye(eye, &packet);
        }
        copiedAnyEye = copiedAnyEye || copied;
    }

    if (!g_clientFrameWriter.Write(&packet, sizeof(packet))) {
        Logf("client frame ring write failed");
        return false;
    }
    Logf("submitted frame=%llu copied=%d target=%llu",
         static_cast<unsigned long long>(packet.header.frameId),
         copiedAnyEye ? 1 : 0,
         static_cast<unsigned long long>(packet.header.targetDisplayNs));
    return true;
}

Instance* GetInstance(XrInstance handle) {
    return reinterpret_cast<Instance*>(handle);
}

Session* GetSession(XrSession handle) {
    return reinterpret_cast<Session*>(handle);
}

Space* GetSpace(XrSpace handle) {
    return reinterpret_cast<Space*>(handle);
}

Swapchain* GetSwapchain(XrSwapchain handle) {
    return reinterpret_cast<Swapchain*>(handle);
}

XrResult CheckInstance(XrInstance handle) {
    return handle != XR_NULL_HANDLE && GetInstance(handle) ? XR_SUCCESS : XR_ERROR_HANDLE_INVALID;
}

XrResult CheckSession(XrSession handle) {
    return handle != XR_NULL_HANDLE && GetSession(handle) ? XR_SUCCESS : XR_ERROR_HANDLE_INVALID;
}

void DestroySessionLocked(Session* sess) {
    if (!sess) {
        return;
    }
    for (auto it = g_swapchains.begin(); it != g_swapchains.end();) {
        if ((*it)->session == sess) {
            delete *it;
            it = g_swapchains.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = g_spaces.begin(); it != g_spaces.end();) {
        if ((*it)->session == sess) {
            delete *it;
            it = g_spaces.erase(it);
        } else {
            ++it;
        }
    }
    g_sessions.erase(std::remove(g_sessions.begin(), g_sessions.end(), sess), g_sessions.end());
    delete sess;
}

} // namespace

extern "C" {

XRAPI_ATTR XrResult XRAPI_CALL xrNegotiateLoaderRuntimeInterface(const XrNegotiateLoaderInfo* loaderInfo,
                                                                 XrNegotiateRuntimeRequest* runtimeRequest) {
    Logf("xrNegotiateLoaderRuntimeInterface loader=%p request=%p", static_cast<const void*>(loaderInfo), static_cast<void*>(runtimeRequest));
    if (!loaderInfo || !runtimeRequest) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    if (loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO ||
        loaderInfo->structVersion != XR_LOADER_INFO_STRUCT_VERSION ||
        loaderInfo->structSize < sizeof(XrNegotiateLoaderInfo)) {
        Logf("loader info validation failed type=%d version=%u size=%zu",
             loaderInfo->structType,
             loaderInfo->structVersion,
             loaderInfo->structSize);
        return XR_ERROR_INITIALIZATION_FAILED;
    }
    if (loaderInfo->maxInterfaceVersion < XR_CURRENT_LOADER_RUNTIME_VERSION ||
        loaderInfo->minInterfaceVersion > XR_CURRENT_LOADER_RUNTIME_VERSION) {
        Logf("loader runtime interface mismatch min=%u max=%u runtime=%u",
             loaderInfo->minInterfaceVersion,
             loaderInfo->maxInterfaceVersion,
             XR_CURRENT_LOADER_RUNTIME_VERSION);
        return XR_ERROR_INITIALIZATION_FAILED;
    }
    if (loaderInfo->maxApiVersion < XR_MAKE_VERSION(1, 0, 0)) {
        return XR_ERROR_API_VERSION_UNSUPPORTED;
    }

    runtimeRequest->structType = XR_LOADER_INTERFACE_STRUCT_RUNTIME_REQUEST;
    runtimeRequest->structVersion = XR_RUNTIME_INFO_STRUCT_VERSION;
    runtimeRequest->structSize = sizeof(XrNegotiateRuntimeRequest);
    runtimeRequest->runtimeInterfaceVersion = XR_CURRENT_LOADER_RUNTIME_VERSION;
    runtimeRequest->runtimeApiVersion = kRuntimeApiVersion;
    runtimeRequest->getInstanceProcAddr = xrGetInstanceProcAddr;
    Logf("negotiation success api=%llu", static_cast<unsigned long long>(runtimeRequest->runtimeApiVersion));
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateInstanceExtensionProperties(const char* layerName,
                                                                      uint32_t propertyCapacityInput,
                                                                      uint32_t* propertyCountOutput,
                                                                      XrExtensionProperties* properties) {
    Logf("xrEnumerateInstanceExtensionProperties layer=%s capacity=%u", layerName ? layerName : "(null)", propertyCapacityInput);
    if (layerName && layerName[0] != '\0') {
        return XR_ERROR_API_LAYER_NOT_PRESENT;
    }

    struct ExtensionInfo {
        const char* name;
        uint32_t version;
    };
    const ExtensionInfo extensions[] = {
        {XR_KHR_OPENGL_ENABLE_EXTENSION_NAME, XR_KHR_opengl_enable_SPEC_VERSION},
        {XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME, XR_KHR_opengl_es_enable_SPEC_VERSION},
    };
    constexpr uint32_t count = sizeof(extensions) / sizeof(extensions[0]);
    if (!propertyCountOutput) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    *propertyCountOutput = count;
    if (propertyCapacityInput == 0) {
        return XR_SUCCESS;
    }
    if (propertyCapacityInput < count) {
        return XR_ERROR_SIZE_INSUFFICIENT;
    }
    for (uint32_t i = 0; i < count; ++i) {
        properties[i].type = XR_TYPE_EXTENSION_PROPERTIES;
        properties[i].next = nullptr;
        CopyString(properties[i].extensionName, XR_MAX_EXTENSION_NAME_SIZE, extensions[i].name);
        properties[i].extensionVersion = extensions[i].version;
    }
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrCreateInstance(const XrInstanceCreateInfo* createInfo, XrInstance* instance) {
    if (!createInfo || !instance) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    Logf("xrCreateInstance app=%s extensions=%u",
         createInfo->applicationInfo.applicationName,
         createInfo->enabledExtensionCount);
    auto* out = new Instance();
    out->id = g_nextId++;
    for (uint32_t i = 0; i < createInfo->enabledExtensionCount; ++i) {
        const char* ext = createInfo->enabledExtensionNames[i];
        if (std::strcmp(ext, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME) != 0 &&
            std::strcmp(ext, XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME) != 0 &&
            std::strcmp(ext, XR_EXT_DEBUG_UTILS_EXTENSION_NAME) != 0) {
            Logf("unsupported extension requested: %s", ext);
            delete out;
            return XR_ERROR_EXTENSION_NOT_PRESENT;
        }
        out->enabledExtensions.emplace_back(ext);
    }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_instances.push_back(out);
    }
    *instance = ToHandle(out);
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrDestroyInstance(XrInstance instance) {
    Logf("xrDestroyInstance %p", reinterpret_cast<void*>(instance));
    auto* inst = GetInstance(instance);
    if (!inst) {
        return XR_ERROR_HANDLE_INVALID;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    for (;;) {
        auto it = std::find_if(g_sessions.begin(), g_sessions.end(), [&](Session* sess) {
            return sess && sess->instance == inst;
        });
        if (it == g_sessions.end()) {
            break;
        }
        DestroySessionLocked(*it);
    }
    g_instances.erase(std::remove(g_instances.begin(), g_instances.end(), inst), g_instances.end());
    delete inst;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetInstanceProperties(XrInstance instance, XrInstanceProperties* instanceProperties) {
    if (CheckInstance(instance) != XR_SUCCESS || !instanceProperties) {
        return XR_ERROR_HANDLE_INVALID;
    }
    instanceProperties->runtimeVersion = XR_MAKE_VERSION(0, 2, 0);
    CopyString(instanceProperties->runtimeName, XR_MAX_RUNTIME_NAME_SIZE, "MirageBridge OpenXR");
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetSystem(XrInstance instance, const XrSystemGetInfo* getInfo, XrSystemId* systemId) {
    Logf("xrGetSystem formFactor=%d", getInfo ? getInfo->formFactor : -1);
    if (CheckInstance(instance) != XR_SUCCESS || !getInfo || !systemId) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (getInfo->formFactor != XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY) {
        return XR_ERROR_FORM_FACTOR_UNSUPPORTED;
    }
    *systemId = kSystemId;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetSystemProperties(XrInstance instance, XrSystemId systemId, XrSystemProperties* properties) {
    if (CheckInstance(instance) != XR_SUCCESS || systemId != kSystemId || !properties) {
        return XR_ERROR_SYSTEM_INVALID;
    }
    properties->systemId = kSystemId;
    properties->vendorId = 0x4D42;
    CopyString(properties->systemName, XR_MAX_SYSTEM_NAME_SIZE, "Lenovo Mirage Solo via MirageBridge");
    properties->graphicsProperties.maxSwapchainImageWidth = 4096;
    properties->graphicsProperties.maxSwapchainImageHeight = 4096;
    properties->graphicsProperties.maxLayerCount = 16;
    properties->trackingProperties.orientationTracking = XR_TRUE;
    properties->trackingProperties.positionTracking = XR_TRUE;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrCreateSession(XrInstance instance, const XrSessionCreateInfo* createInfo, XrSession* session) {
    if (CheckInstance(instance) != XR_SUCCESS || !createInfo || !session || createInfo->systemId != kSystemId) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    auto* inst = GetInstance(instance);
    auto* out = new Session();
    out->id = g_nextId++;
    out->instance = inst;
    for (const XrBaseInStructure* entry = reinterpret_cast<const XrBaseInStructure*>(createInfo->next); entry; entry = entry->next) {
        if (entry->type == XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR ||
            entry->type == XR_TYPE_GRAPHICS_BINDING_OPENGL_XCB_KHR ||
            entry->type == XR_TYPE_GRAPHICS_BINDING_OPENGL_WAYLAND_KHR ||
            entry->type == XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR) {
            out->usesOpenGL = true;
        }
        if (entry->type == XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR) {
            out->usesOpenGLES = true;
        }
        Logf("xrCreateSession graphics binding type=%d", entry->type);
    }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_sessions.push_back(out);
        PushSessionState(out, XR_SESSION_STATE_READY);
    }
    *session = ToHandle(out);
    Logf("xrCreateSession success session=%p opengl=%d gles=%d", static_cast<void*>(out), out->usesOpenGL, out->usesOpenGLES);
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrDestroySession(XrSession session) {
    auto* sess = GetSession(session);
    if (!sess) {
        return XR_ERROR_HANDLE_INVALID;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    DestroySessionLocked(sess);
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrBeginSession(XrSession session, const XrSessionBeginInfo* beginInfo) {
    auto* sess = GetSession(session);
    if (!sess || !beginInfo) {
        return XR_ERROR_HANDLE_INVALID;
    }
    Logf("xrBeginSession viewConfig=%d", beginInfo->primaryViewConfigurationType);
    sess->running = true;
    std::lock_guard<std::mutex> lock(g_mutex);
    PushSessionState(sess, XR_SESSION_STATE_FOCUSED);
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEndSession(XrSession session) {
    auto* sess = GetSession(session);
    if (!sess) {
        return XR_ERROR_HANDLE_INVALID;
    }
    sess->running = false;
    std::lock_guard<std::mutex> lock(g_mutex);
    PushSessionState(sess, XR_SESSION_STATE_STOPPING);
    PushSessionState(sess, XR_SESSION_STATE_IDLE);
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrRequestExitSession(XrSession session) {
    auto* sess = GetSession(session);
    if (!sess) {
        return XR_ERROR_HANDLE_INVALID;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    PushSessionState(sess, XR_SESSION_STATE_STOPPING);
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrPollEvent(XrInstance instance, XrEventDataBuffer* eventData) {
    if (CheckInstance(instance) != XR_SUCCESS || !eventData) {
        return XR_ERROR_HANDLE_INVALID;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_events.empty()) {
        eventData->type = XR_TYPE_EVENT_DATA_BUFFER;
        return XR_EVENT_UNAVAILABLE;
    }
    XrEventDataSessionStateChanged event = g_events.front();
    g_events.erase(g_events.begin());
    std::memset(eventData, 0, sizeof(*eventData));
    std::memcpy(eventData, &event, sizeof(event));
    Logf("xrPollEvent session state=%d", event.state);
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateReferenceSpaces(XrSession session, uint32_t spaceCapacityInput, uint32_t* spaceCountOutput, XrReferenceSpaceType* spaces) {
    if (CheckSession(session) != XR_SUCCESS) {
        return XR_ERROR_HANDLE_INVALID;
    }
    const XrReferenceSpaceType values[] = {XR_REFERENCE_SPACE_TYPE_VIEW, XR_REFERENCE_SPACE_TYPE_LOCAL, XR_REFERENCE_SPACE_TYPE_STAGE};
    return CopyCounted(spaceCapacityInput, spaceCountOutput, 3, [&](uint32_t i) { spaces[i] = values[i]; }) ? XR_SUCCESS : XR_ERROR_SIZE_INSUFFICIENT;
}

XRAPI_ATTR XrResult XRAPI_CALL xrCreateReferenceSpace(XrSession session, const XrReferenceSpaceCreateInfo* createInfo, XrSpace* space) {
    if (CheckSession(session) != XR_SUCCESS || !createInfo || !space) {
        return XR_ERROR_HANDLE_INVALID;
    }
    auto* out = new Space();
    out->id = g_nextId++;
    out->session = GetSession(session);
    out->type = createInfo->referenceSpaceType;
    out->pose = createInfo->poseInReferenceSpace;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_spaces.push_back(out);
    *space = ToHandle(out);
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrDestroySpace(XrSpace space) {
    auto* sp = GetSpace(space);
    if (!sp) {
        return XR_ERROR_HANDLE_INVALID;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_spaces.erase(std::remove(g_spaces.begin(), g_spaces.end(), sp), g_spaces.end());
    delete sp;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrLocateSpace(XrSpace space, XrSpace, XrTime time, XrSpaceLocation* location) {
    auto* sp = GetSpace(space);
    if (!sp || !location) {
        return XR_ERROR_HANDLE_INVALID;
    }
    location->locationFlags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT |
                              XR_SPACE_LOCATION_POSITION_VALID_BIT |
                              XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT |
                              XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
    location->pose = PredictHeadPose(time);
    location->pose.orientation = MulQuat(location->pose.orientation, sp->pose.orientation);
    location->pose.position.x += sp->pose.position.x;
    location->pose.position.y += sp->pose.position.y;
    location->pose.position.z += sp->pose.position.z;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateViewConfigurations(XrInstance instance, XrSystemId systemId, uint32_t capacity, uint32_t* count, XrViewConfigurationType* types) {
    if (CheckInstance(instance) != XR_SUCCESS || systemId != kSystemId) {
        return XR_ERROR_SYSTEM_INVALID;
    }
    return CopyCounted(capacity, count, 1, [&](uint32_t) { types[0] = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO; }) ? XR_SUCCESS : XR_ERROR_SIZE_INSUFFICIENT;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetViewConfigurationProperties(XrInstance instance, XrSystemId systemId, XrViewConfigurationType viewConfigurationType, XrViewConfigurationProperties* properties) {
    if (CheckInstance(instance) != XR_SUCCESS || systemId != kSystemId || !properties) {
        return XR_ERROR_SYSTEM_INVALID;
    }
    properties->viewConfigurationType = viewConfigurationType;
    properties->fovMutable = XR_FALSE;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateViewConfigurationViews(XrInstance instance,
                                                                 XrSystemId systemId,
                                                                 XrViewConfigurationType,
                                                                 uint32_t viewCapacityInput,
                                                                 uint32_t* viewCountOutput,
                                                                 XrViewConfigurationView* views) {
    if (CheckInstance(instance) != XR_SUCCESS || systemId != kSystemId) {
        return XR_ERROR_SYSTEM_INVALID;
    }
    if (!viewCountOutput) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    *viewCountOutput = kViewCount;
    if (viewCapacityInput == 0) {
        return XR_SUCCESS;
    }
    if (viewCapacityInput < kViewCount) {
        return XR_ERROR_SIZE_INSUFFICIENT;
    }
    for (uint32_t i = 0; i < kViewCount; ++i) {
        views[i].recommendedImageRectWidth = miragebridge::kSbsWidth / 2;
        views[i].maxImageRectWidth = miragebridge::kSbsWidth / 2;
        views[i].recommendedImageRectHeight = miragebridge::kSbsHeight;
        views[i].maxImageRectHeight = miragebridge::kSbsHeight;
        views[i].recommendedSwapchainSampleCount = 1;
        views[i].maxSwapchainSampleCount = 1;
    }
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateEnvironmentBlendModes(XrInstance instance,
                                                                XrSystemId systemId,
                                                                XrViewConfigurationType,
                                                                uint32_t capacity,
                                                                uint32_t* count,
                                                                XrEnvironmentBlendMode* modes) {
    if (CheckInstance(instance) != XR_SUCCESS || systemId != kSystemId) {
        return XR_ERROR_SYSTEM_INVALID;
    }
    return CopyCounted(capacity, count, 1, [&](uint32_t) { modes[0] = XR_ENVIRONMENT_BLEND_MODE_OPAQUE; }) ? XR_SUCCESS : XR_ERROR_SIZE_INSUFFICIENT;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateSwapchainFormats(XrSession session, uint32_t capacity, uint32_t* count, int64_t* formats) {
    if (CheckSession(session) != XR_SUCCESS) {
        return XR_ERROR_HANDLE_INVALID;
    }
    const int64_t values[] = {GL_SRGB8_ALPHA8_MB, GL_RGBA8_MB};
    return CopyCounted(capacity, count, 2, [&](uint32_t i) { formats[i] = values[i]; }) ? XR_SUCCESS : XR_ERROR_SIZE_INSUFFICIENT;
}

XRAPI_ATTR XrResult XRAPI_CALL xrCreateSwapchain(XrSession session, const XrSwapchainCreateInfo* createInfo, XrSwapchain* swapchain) {
    auto* sess = GetSession(session);
    if (!sess || !createInfo || !swapchain) {
        return XR_ERROR_HANDLE_INVALID;
    }
    auto* out = new Swapchain();
    out->id = g_nextId++;
    out->session = sess;
    out->createInfo = *createInfo;
    out->glImages.resize(kSwapchainImageCount);
    for (uint32_t& image : out->glImages) {
        CreateOpenGLTexture(createInfo->width, createInfo->height, createInfo->format, &image);
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_swapchains.push_back(out);
    *swapchain = ToHandle(out);
    Logf("xrCreateSwapchain %ux%u format=%lld handle=%p", createInfo->width, createInfo->height, static_cast<long long>(createInfo->format), static_cast<void*>(out));
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrDestroySwapchain(XrSwapchain swapchain) {
    auto* sc = GetSwapchain(swapchain);
    if (!sc) {
        return XR_ERROR_HANDLE_INVALID;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_swapchains.erase(std::remove(g_swapchains.begin(), g_swapchains.end(), sc), g_swapchains.end());
    delete sc;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateSwapchainImages(XrSwapchain swapchain, uint32_t capacity, uint32_t* count, XrSwapchainImageBaseHeader* images) {
    auto* sc = GetSwapchain(swapchain);
    if (!sc || !count) {
        return XR_ERROR_HANDLE_INVALID;
    }
    *count = static_cast<uint32_t>(sc->glImages.size());
    if (capacity == 0) {
        return XR_SUCCESS;
    }
    if (capacity < sc->glImages.size()) {
        return XR_ERROR_SIZE_INSUFFICIENT;
    }
    for (uint32_t i = 0; i < sc->glImages.size(); ++i) {
        if (images[i].type == XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR) {
            auto* image = reinterpret_cast<XrSwapchainImageOpenGLKHR*>(&images[i]);
            image->image = sc->glImages[i];
        } else if (images[i].type == XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR) {
            auto* image = reinterpret_cast<XrSwapchainImageOpenGLESKHR*>(&images[i]);
            image->image = sc->glImages[i];
        }
    }
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrAcquireSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageAcquireInfo*, uint32_t* index) {
    auto* sc = GetSwapchain(swapchain);
    if (!sc || !index) {
        return XR_ERROR_HANDLE_INVALID;
    }
    sc->acquiredIndex = (sc->acquiredIndex + 1) % static_cast<uint32_t>(sc->glImages.size());
    sc->imageAcquired = true;
    *index = sc->acquiredIndex;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrWaitSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageWaitInfo*) {
    return GetSwapchain(swapchain) ? XR_SUCCESS : XR_ERROR_HANDLE_INVALID;
}

XRAPI_ATTR XrResult XRAPI_CALL xrReleaseSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageReleaseInfo*) {
    auto* sc = GetSwapchain(swapchain);
    if (!sc) {
        return XR_ERROR_HANDLE_INVALID;
    }
    sc->releasedIndex = sc->acquiredIndex;
    sc->imageAcquired = false;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrWaitFrame(XrSession session, const XrFrameWaitInfo*, XrFrameState* frameState) {
    auto* sess = GetSession(session);
    if (!sess || !frameState) {
        return XR_ERROR_HANDLE_INVALID;
    }
    RefreshPose();
    const uint64_t period = g_lastPose.displayHz ? (1000000000ull / g_lastPose.displayHz) : static_cast<uint64_t>(kDisplayPeriodNs);
    const uint64_t now = MonotonicNs();
    const uint64_t predicted = g_lastPose.predictedDisplayNs && g_lastPose.predictedDisplayNs > now
        ? g_lastPose.predictedDisplayNs
        : now + period;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    sess->predictedDisplayTime = static_cast<XrTime>(predicted);
    sess->predictedDisplayPeriod = static_cast<XrDuration>(period);
    sess->frameWaited = true;
    frameState->predictedDisplayTime = sess->predictedDisplayTime;
    frameState->predictedDisplayPeriod = sess->predictedDisplayPeriod;
    frameState->shouldRender = XR_TRUE;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrBeginFrame(XrSession session, const XrFrameBeginInfo*) {
    auto* sess = GetSession(session);
    if (!sess) {
        return XR_ERROR_HANDLE_INVALID;
    }
    sess->frameBegun = true;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEndFrame(XrSession session, const XrFrameEndInfo* frameEndInfo) {
    auto* sess = GetSession(session);
    if (!sess || !frameEndInfo) {
        return XR_ERROR_HANDLE_INVALID;
    }
    SubmitProjectionLayerToBridge(sess, frameEndInfo);
    sess->frameId++;
    sess->frameWaited = false;
    sess->frameBegun = false;
    Logf("xrEndFrame layers=%u", frameEndInfo ? frameEndInfo->layerCount : 0);
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrLocateViews(XrSession session,
                                             const XrViewLocateInfo* viewLocateInfo,
                                             XrViewState* viewState,
                                             uint32_t viewCapacityInput,
                                             uint32_t* viewCountOutput,
                                             XrView* views) {
    if (CheckSession(session) != XR_SUCCESS || !viewCountOutput) {
        return XR_ERROR_HANDLE_INVALID;
    }
    *viewCountOutput = kViewCount;
    if (viewState) {
        viewState->viewStateFlags = XR_VIEW_STATE_ORIENTATION_VALID_BIT |
                                    XR_VIEW_STATE_POSITION_VALID_BIT |
                                    XR_VIEW_STATE_ORIENTATION_TRACKED_BIT |
                                    XR_VIEW_STATE_POSITION_TRACKED_BIT;
    }
    if (viewCapacityInput == 0) {
        return XR_SUCCESS;
    }
    if (viewCapacityInput < kViewCount || !views) {
        return XR_ERROR_SIZE_INSUFFICIENT;
    }
    for (uint32_t i = 0; i < kViewCount; ++i) {
        FillPoseFromPacket(i, viewLocateInfo ? viewLocateInfo->displayTime : 0, &views[i].pose, &views[i].fov);
    }
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetOpenGLGraphicsRequirementsKHR(XrInstance instance, XrSystemId systemId, XrGraphicsRequirementsOpenGLKHR* requirements) {
    if (CheckInstance(instance) != XR_SUCCESS || systemId != kSystemId || !requirements) {
        return XR_ERROR_SYSTEM_INVALID;
    }
    requirements->minApiVersionSupported = XR_MAKE_VERSION(3, 2, 0);
    requirements->maxApiVersionSupported = XR_MAKE_VERSION(4, 6, 0);
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetOpenGLESGraphicsRequirementsKHR(XrInstance instance, XrSystemId systemId, XrGraphicsRequirementsOpenGLESKHR* requirements) {
    if (CheckInstance(instance) != XR_SUCCESS || systemId != kSystemId || !requirements) {
        return XR_ERROR_SYSTEM_INVALID;
    }
    requirements->minApiVersionSupported = XR_MAKE_VERSION(3, 0, 0);
    requirements->maxApiVersionSupported = XR_MAKE_VERSION(3, 2, 0);
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrStringToPath(XrInstance instance, const char* pathString, XrPath* path) {
    if (CheckInstance(instance) != XR_SUCCESS || !pathString || !path) {
        return XR_ERROR_HANDLE_INVALID;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_paths.find(pathString);
    if (it != g_paths.end()) {
        *path = it->second;
        return XR_SUCCESS;
    }
    XrPath newPath = g_nextPath++;
    g_paths[pathString] = newPath;
    g_pathNames[newPath] = pathString;
    *path = newPath;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrPathToString(XrInstance instance, XrPath path, uint32_t capacity, uint32_t* count, char* buffer) {
    if (CheckInstance(instance) != XR_SUCCESS || !count) {
        return XR_ERROR_HANDLE_INVALID;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    std::string value = g_pathNames.count(path) ? g_pathNames[path] : "";
    *count = static_cast<uint32_t>(value.size() + 1);
    if (capacity == 0) {
        return XR_SUCCESS;
    }
    if (capacity < *count) {
        return XR_ERROR_SIZE_INSUFFICIENT;
    }
    std::memcpy(buffer, value.c_str(), *count);
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrResultToString(XrInstance, XrResult value, char buffer[XR_MAX_RESULT_STRING_SIZE]) {
    CopyString(buffer, XR_MAX_RESULT_STRING_SIZE, value == XR_SUCCESS ? "XR_SUCCESS" : "XR_RESULT");
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrStructureTypeToString(XrInstance, XrStructureType value, char buffer[XR_MAX_STRUCTURE_NAME_SIZE]) {
    char tmp[XR_MAX_STRUCTURE_NAME_SIZE];
    std::snprintf(tmp, sizeof(tmp), "XrStructureType(%d)", value);
    CopyString(buffer, XR_MAX_STRUCTURE_NAME_SIZE, tmp);
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrCreateActionSet(XrInstance, const XrActionSetCreateInfo*, XrActionSet* actionSet) {
    *actionSet = reinterpret_cast<XrActionSet>(new uint64_t(g_nextId++));
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrDestroyActionSet(XrActionSet actionSet) {
    delete reinterpret_cast<uint64_t*>(actionSet);
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrCreateAction(XrActionSet, const XrActionCreateInfo*, XrAction* action) {
    *action = reinterpret_cast<XrAction>(new uint64_t(g_nextId++));
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrDestroyAction(XrAction action) {
    delete reinterpret_cast<uint64_t*>(action);
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrSuggestInteractionProfileBindings(XrInstance, const XrInteractionProfileSuggestedBinding*) {
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrAttachSessionActionSets(XrSession, const XrSessionActionSetsAttachInfo*) {
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrSyncActions(XrSession, const XrActionsSyncInfo*) {
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateBoolean(XrSession, const XrActionStateGetInfo*, XrActionStateBoolean* state) {
    if (!state) return XR_ERROR_VALIDATION_FAILURE;
    state->currentState = XR_FALSE;
    state->changedSinceLastSync = XR_FALSE;
    state->lastChangeTime = static_cast<XrTime>(MonotonicNs());
    state->isActive = XR_FALSE;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateFloat(XrSession, const XrActionStateGetInfo*, XrActionStateFloat* state) {
    if (!state) return XR_ERROR_VALIDATION_FAILURE;
    state->currentState = 0.0f;
    state->changedSinceLastSync = XR_FALSE;
    state->lastChangeTime = static_cast<XrTime>(MonotonicNs());
    state->isActive = XR_FALSE;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateVector2f(XrSession, const XrActionStateGetInfo*, XrActionStateVector2f* state) {
    if (!state) return XR_ERROR_VALIDATION_FAILURE;
    state->currentState = {0.0f, 0.0f};
    state->changedSinceLastSync = XR_FALSE;
    state->lastChangeTime = static_cast<XrTime>(MonotonicNs());
    state->isActive = XR_FALSE;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStatePose(XrSession, const XrActionStateGetInfo*, XrActionStatePose* state) {
    if (!state) return XR_ERROR_VALIDATION_FAILURE;
    state->isActive = XR_FALSE;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetCurrentInteractionProfile(XrSession, XrPath, XrInteractionProfileState* state) {
    if (!state) return XR_ERROR_VALIDATION_FAILURE;
    state->interactionProfile = XR_NULL_PATH;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateBoundSourcesForAction(XrSession, const XrBoundSourcesForActionEnumerateInfo*, uint32_t capacity, uint32_t* count, XrPath*) {
    if (!count) return XR_ERROR_VALIDATION_FAILURE;
    *count = 0;
    return capacity == 0 ? XR_SUCCESS : XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetInputSourceLocalizedName(XrSession, const XrInputSourceLocalizedNameGetInfo*, uint32_t capacity, uint32_t* count, char* buffer) {
    const char* name = "";
    if (!count) return XR_ERROR_VALIDATION_FAILURE;
    *count = 1;
    if (capacity == 0) return XR_SUCCESS;
    if (capacity < 1) return XR_ERROR_SIZE_INSUFFICIENT;
    buffer[0] = '\0';
    (void)name;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrApplyHapticFeedback(XrSession, const XrHapticActionInfo*, const XrHapticBaseHeader*) {
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrStopHapticFeedback(XrSession, const XrHapticActionInfo*) {
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrCreateActionSpace(XrSession session, const XrActionSpaceCreateInfo* createInfo, XrSpace* space) {
    XrReferenceSpaceCreateInfo info{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    info.poseInReferenceSpace = createInfo ? createInfo->poseInActionSpace : XrPosef{{0, 0, 0, 1}, {0, 0, 0}};
    return xrCreateReferenceSpace(session, &info, space);
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetReferenceSpaceBoundsRect(XrSession, XrReferenceSpaceType, XrExtent2Df* bounds) {
    if (!bounds) return XR_ERROR_VALIDATION_FAILURE;
    bounds->width = 2.0f;
    bounds->height = 2.0f;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetInstanceProcAddr(XrInstance instance, const char* name, PFN_xrVoidFunction* function) {
    if (!name || !function) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    *function = nullptr;

#define MBXR_ENTRY(fn) if (std::strcmp(name, #fn) == 0) { *function = reinterpret_cast<PFN_xrVoidFunction>(fn); Logf("xrGetInstanceProcAddr %s -> %p", name, reinterpret_cast<void*>(*function)); return XR_SUCCESS; }
    MBXR_ENTRY(xrGetInstanceProcAddr)
    MBXR_ENTRY(xrEnumerateInstanceExtensionProperties)
    MBXR_ENTRY(xrCreateInstance)
    MBXR_ENTRY(xrDestroyInstance)
    MBXR_ENTRY(xrGetInstanceProperties)
    MBXR_ENTRY(xrPollEvent)
    MBXR_ENTRY(xrResultToString)
    MBXR_ENTRY(xrStructureTypeToString)
    MBXR_ENTRY(xrGetSystem)
    MBXR_ENTRY(xrGetSystemProperties)
    MBXR_ENTRY(xrEnumerateEnvironmentBlendModes)
    MBXR_ENTRY(xrCreateSession)
    MBXR_ENTRY(xrDestroySession)
    MBXR_ENTRY(xrEnumerateReferenceSpaces)
    MBXR_ENTRY(xrCreateReferenceSpace)
    MBXR_ENTRY(xrGetReferenceSpaceBoundsRect)
    MBXR_ENTRY(xrCreateActionSpace)
    MBXR_ENTRY(xrLocateSpace)
    MBXR_ENTRY(xrDestroySpace)
    MBXR_ENTRY(xrEnumerateViewConfigurations)
    MBXR_ENTRY(xrGetViewConfigurationProperties)
    MBXR_ENTRY(xrEnumerateViewConfigurationViews)
    MBXR_ENTRY(xrEnumerateSwapchainFormats)
    MBXR_ENTRY(xrCreateSwapchain)
    MBXR_ENTRY(xrDestroySwapchain)
    MBXR_ENTRY(xrEnumerateSwapchainImages)
    MBXR_ENTRY(xrAcquireSwapchainImage)
    MBXR_ENTRY(xrWaitSwapchainImage)
    MBXR_ENTRY(xrReleaseSwapchainImage)
    MBXR_ENTRY(xrBeginSession)
    MBXR_ENTRY(xrEndSession)
    MBXR_ENTRY(xrRequestExitSession)
    MBXR_ENTRY(xrWaitFrame)
    MBXR_ENTRY(xrBeginFrame)
    MBXR_ENTRY(xrEndFrame)
    MBXR_ENTRY(xrLocateViews)
    MBXR_ENTRY(xrStringToPath)
    MBXR_ENTRY(xrPathToString)
    MBXR_ENTRY(xrCreateActionSet)
    MBXR_ENTRY(xrDestroyActionSet)
    MBXR_ENTRY(xrCreateAction)
    MBXR_ENTRY(xrDestroyAction)
    MBXR_ENTRY(xrSuggestInteractionProfileBindings)
    MBXR_ENTRY(xrAttachSessionActionSets)
    MBXR_ENTRY(xrGetCurrentInteractionProfile)
    MBXR_ENTRY(xrGetActionStateBoolean)
    MBXR_ENTRY(xrGetActionStateFloat)
    MBXR_ENTRY(xrGetActionStateVector2f)
    MBXR_ENTRY(xrGetActionStatePose)
    MBXR_ENTRY(xrSyncActions)
    MBXR_ENTRY(xrEnumerateBoundSourcesForAction)
    MBXR_ENTRY(xrGetInputSourceLocalizedName)
    MBXR_ENTRY(xrApplyHapticFeedback)
    MBXR_ENTRY(xrStopHapticFeedback)
    MBXR_ENTRY(xrGetOpenGLGraphicsRequirementsKHR)
    MBXR_ENTRY(xrGetOpenGLESGraphicsRequirementsKHR)
#undef MBXR_ENTRY

    (void)instance;
    Logf("xrGetInstanceProcAddr %s -> unsupported", name);
    return XR_ERROR_FUNCTION_UNSUPPORTED;
}

} // extern "C"
