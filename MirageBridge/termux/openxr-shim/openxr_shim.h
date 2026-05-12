#pragma once

#include <cstdint>

#include "../../common/miragebridge_protocol.h"

typedef uint64_t XrSession;
typedef uint64_t XrInstance;
typedef uint64_t XrSystemId;
typedef int32_t XrResult;
typedef int64_t XrTime;
typedef void (*PFN_xrVoidFunction)(void);

constexpr XrResult XR_SUCCESS = 0;
constexpr XrResult XR_ERROR_FUNCTION_UNSUPPORTED = -7;

struct XrFrameWaitInfo {};
struct XrFrameState {
    XrTime predictedDisplayTime;
    XrTime predictedDisplayPeriod;
    uint32_t shouldRender;
};
struct XrFrameBeginInfo {};
struct XrFrameEndInfo {};
struct XrViewLocateInfo {};
struct XrViewState {
    uint32_t viewStateFlags;
};
struct XrView {
    float posePosition[3];
    float poseOrientation[4];
    float fov[4];
};
struct XrViewConfigurationView {
    uint32_t recommendedImageRectWidth;
    uint32_t maxImageRectWidth;
    uint32_t recommendedImageRectHeight;
    uint32_t maxImageRectHeight;
    uint32_t recommendedSwapchainSampleCount;
    uint32_t maxSwapchainSampleCount;
};

extern "C" {
XrResult xrGetInstanceProcAddr(XrInstance instance, const char* name, PFN_xrVoidFunction* function);
XrResult xrEnumerateViewConfigurationViews(XrInstance instance, XrSystemId systemId, uint32_t viewConfigurationType, uint32_t viewCapacityInput, uint32_t* viewCountOutput, XrViewConfigurationView* views);
XrResult xrWaitFrame(XrSession session, const XrFrameWaitInfo* frameWaitInfo, XrFrameState* frameState);
XrResult xrBeginFrame(XrSession session, const XrFrameBeginInfo* frameBeginInfo);
XrResult xrEndFrame(XrSession session, const XrFrameEndInfo* frameEndInfo);
XrResult xrLocateViews(XrSession session, const XrViewLocateInfo* viewLocateInfo, XrViewState* viewState, uint32_t viewCapacityInput, uint32_t* viewCountOutput, XrView* views);
}
