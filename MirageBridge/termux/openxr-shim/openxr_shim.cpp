#include "openxr_shim.h"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "miragebridge_protocol.h"
#include "transport_reader.h"

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
};

struct Space {
    uint64_t id = 0;
    XrReferenceSpaceType type = XR_REFERENCE_SPACE_TYPE_LOCAL;
    XrPosef pose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
};

struct Swapchain {
    uint64_t id = 0;
    Session* session = nullptr;
    XrSwapchainCreateInfo createInfo{};
    uint32_t acquiredIndex = 0;
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
miragebridge::XRPacket g_lastPose{};
uint64_t g_lastPoseSeq = 0;
uint64_t g_nextPath = 1;
std::unordered_map<std::string, XrPath> g_paths;
std::unordered_map<XrPath, std::string> g_pathNames;

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

void FillPoseFromPacket(uint32_t eye, XrPosef* pose, XrFovf* fov) {
    RefreshPose();
    const float eyeOffset = g_lastPose.eyes[eye].view[3] != 0.0f ? g_lastPose.eyes[eye].view[3] : (eye == 0 ? -0.032f : 0.032f);
    pose->orientation = {g_lastPose.rot[0], g_lastPose.rot[1], g_lastPose.rot[2], g_lastPose.rot[3]};
    pose->position = {g_lastPose.pos[0] + eyeOffset, g_lastPose.pos[1], g_lastPose.pos[2]};
    fov->angleLeft = g_lastPose.eyes[eye].fov[0] != 0.0f ? g_lastPose.eyes[eye].fov[0] : -0.75f;
    fov->angleRight = g_lastPose.eyes[eye].fov[1] != 0.0f ? g_lastPose.eyes[eye].fov[1] : 0.75f;
    fov->angleUp = g_lastPose.eyes[eye].fov[2] != 0.0f ? g_lastPose.eyes[eye].fov[2] : 0.75f;
    fov->angleDown = g_lastPose.eyes[eye].fov[3] != 0.0f ? g_lastPose.eyes[eye].fov[3] : -0.75f;
}

using PFN_glGenTextures = void (*)(int, uint32_t*);
using PFN_glBindTexture = void (*)(uint32_t, uint32_t);
using PFN_glTexParameteri = void (*)(uint32_t, uint32_t, int);
using PFN_glTexImage2D = void (*)(uint32_t, int, int, int, int, int, uint32_t, uint32_t, const void*);

constexpr uint32_t GL_TEXTURE_2D_MB = 0x0DE1;
constexpr uint32_t GL_TEXTURE_MIN_FILTER_MB = 0x2801;
constexpr uint32_t GL_TEXTURE_MAG_FILTER_MB = 0x2800;
constexpr uint32_t GL_TEXTURE_WRAP_S_MB = 0x2802;
constexpr uint32_t GL_TEXTURE_WRAP_T_MB = 0x2803;
constexpr uint32_t GL_LINEAR_MB = 0x2601;
constexpr uint32_t GL_CLAMP_TO_EDGE_MB = 0x812F;
constexpr uint32_t GL_RGBA_MB = 0x1908;
constexpr uint32_t GL_UNSIGNED_BYTE_MB = 0x1401;
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
    g_sessions.erase(std::remove(g_sessions.begin(), g_sessions.end(), sess), g_sessions.end());
    delete sess;
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

XRAPI_ATTR XrResult XRAPI_CALL xrLocateSpace(XrSpace space, XrSpace, XrTime, XrSpaceLocation* location) {
    if (!GetSpace(space) || !location) {
        return XR_ERROR_HANDLE_INVALID;
    }
    RefreshPose();
    location->locationFlags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT |
                              XR_SPACE_LOCATION_POSITION_VALID_BIT |
                              XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT |
                              XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
    location->pose.orientation = {g_lastPose.rot[0], g_lastPose.rot[1], g_lastPose.rot[2], g_lastPose.rot[3]};
    location->pose.position = {g_lastPose.pos[0], g_lastPose.pos[1], g_lastPose.pos[2]};
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
    sc->imageAcquired = false;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrWaitFrame(XrSession session, const XrFrameWaitInfo*, XrFrameState* frameState) {
    if (CheckSession(session) != XR_SUCCESS || !frameState) {
        return XR_ERROR_HANDLE_INVALID;
    }
    RefreshPose();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    frameState->predictedDisplayTime = static_cast<XrTime>(g_lastPose.predictedDisplayNs ? g_lastPose.predictedDisplayNs : MonotonicNs() + kDisplayPeriodNs);
    frameState->predictedDisplayPeriod = kDisplayPeriodNs;
    frameState->shouldRender = XR_TRUE;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrBeginFrame(XrSession session, const XrFrameBeginInfo*) {
    return CheckSession(session);
}

XRAPI_ATTR XrResult XRAPI_CALL xrEndFrame(XrSession session, const XrFrameEndInfo* frameEndInfo) {
    if (CheckSession(session) != XR_SUCCESS) {
        return XR_ERROR_HANDLE_INVALID;
    }
    Logf("xrEndFrame layers=%u", frameEndInfo ? frameEndInfo->layerCount : 0);
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrLocateViews(XrSession session,
                                             const XrViewLocateInfo*,
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
        FillPoseFromPacket(i, &views[i].pose, &views[i].fov);
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
