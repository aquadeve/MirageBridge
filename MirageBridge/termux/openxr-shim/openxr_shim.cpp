#include "openxr_shim.h"

#include <chrono>
#include <cstring>
#include <thread>

#include "transport_reader.h"

namespace {
miragebridge::RingReader g_reader;
bool g_init = false;
miragebridge::XRPacket g_last{};

void EnsureReader() {
    if (!g_init) {
        const auto cfg = miragebridge::DefaultConfig();
        g_reader.Open(cfg.trackingName, sizeof(miragebridge::XRPacket));
        g_init = true;
    }
}

void RefreshPose() {
    EnsureReader();
    uint64_t seq = 0;
    g_reader.ReadLatest(&g_last, sizeof(g_last), &seq);
}
}

extern "C" XrResult xrGetInstanceProcAddr(XrInstance, const char* name, PFN_xrVoidFunction* function) {
    if (!function || !name) {
        return XR_ERROR_FUNCTION_UNSUPPORTED;
    }
    *function = nullptr;
#define MB_XR_ENTRY(fn) \
    if (std::strcmp(name, #fn) == 0) { \
        *function = reinterpret_cast<PFN_xrVoidFunction>(fn); \
        return XR_SUCCESS; \
    }
    MB_XR_ENTRY(xrGetInstanceProcAddr)
    MB_XR_ENTRY(xrEnumerateViewConfigurationViews)
    MB_XR_ENTRY(xrWaitFrame)
    MB_XR_ENTRY(xrBeginFrame)
    MB_XR_ENTRY(xrEndFrame)
    MB_XR_ENTRY(xrLocateViews)
#undef MB_XR_ENTRY
    return XR_ERROR_FUNCTION_UNSUPPORTED;
}

extern "C" XrResult xrEnumerateViewConfigurationViews(XrInstance,
                                                       XrSystemId,
                                                       uint32_t,
                                                       uint32_t viewCapacityInput,
                                                       uint32_t* viewCountOutput,
                                                       XrViewConfigurationView* views) {
    if (viewCountOutput) {
        *viewCountOutput = 2;
    }
    if (!views || viewCapacityInput < 2) {
        return XR_SUCCESS;
    }
    for (uint32_t i = 0; i < 2; ++i) {
        views[i].recommendedImageRectWidth = miragebridge::kSbsWidth / 2;
        views[i].maxImageRectWidth = miragebridge::kSbsWidth / 2;
        views[i].recommendedImageRectHeight = miragebridge::kSbsHeight;
        views[i].maxImageRectHeight = miragebridge::kSbsHeight;
        views[i].recommendedSwapchainSampleCount = 1;
        views[i].maxSwapchainSampleCount = 1;
    }
    return XR_SUCCESS;
}

extern "C" XrResult xrWaitFrame(XrSession, const XrFrameWaitInfo*, XrFrameState* frameState) {
    RefreshPose();
    std::this_thread::sleep_for(std::chrono::microseconds(1000));
    if (frameState) {
        frameState->predictedDisplayTime = static_cast<XrTime>(g_last.predictedDisplayNs);
        frameState->predictedDisplayPeriod = 13888888;
        frameState->shouldRender = 1;
    }
    return XR_SUCCESS;
}

extern "C" XrResult xrBeginFrame(XrSession, const XrFrameBeginInfo*) {
    return XR_SUCCESS;
}

extern "C" XrResult xrEndFrame(XrSession, const XrFrameEndInfo*) {
    return XR_SUCCESS;
}

extern "C" XrResult xrLocateViews(XrSession, const XrViewLocateInfo*, XrViewState* viewState, uint32_t viewCapacityInput, uint32_t* viewCountOutput, XrView* views) {
    RefreshPose();

    if (viewState) {
        viewState->viewStateFlags = 0x00000001u | 0x00000002u | 0x00000004u | 0x00000008u;
    }

    const uint32_t need = 2;
    if (viewCountOutput) {
        *viewCountOutput = need;
    }
    if (!views || viewCapacityInput < need) {
        return 0;
    }

    for (uint32_t i = 0; i < 2; ++i) {
        const float eyeOffset = g_last.eyes[i].view[3] != 0.0f ? g_last.eyes[i].view[3] : (i == 0 ? -0.032f : 0.032f);
        views[i].posePosition[0] = g_last.pos[0] + eyeOffset;
        views[i].posePosition[1] = g_last.pos[1];
        views[i].posePosition[2] = g_last.pos[2];
        views[i].poseOrientation[0] = g_last.rot[0];
        views[i].poseOrientation[1] = g_last.rot[1];
        views[i].poseOrientation[2] = g_last.rot[2];
        views[i].poseOrientation[3] = g_last.rot[3];
        views[i].fov[0] = g_last.eyes[i].fov[0];
        views[i].fov[1] = g_last.eyes[i].fov[1];
        views[i].fov[2] = g_last.eyes[i].fov[2];
        views[i].fov[3] = g_last.eyes[i].fov[3];
    }

    return XR_SUCCESS;
}
