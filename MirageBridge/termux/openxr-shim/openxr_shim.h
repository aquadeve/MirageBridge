#pragma once

#include <cstdint>

#include "../../common/miragebridge_protocol.h"

typedef uint64_t XrSession;
typedef int32_t XrResult;
typedef int64_t XrTime;

struct XrFrameWaitInfo {};
struct XrFrameState {
    XrTime predictedDisplayTime;
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

extern "C" {
XrResult xrWaitFrame(XrSession session, const XrFrameWaitInfo* frameWaitInfo, XrFrameState* frameState);
XrResult xrBeginFrame(XrSession session, const XrFrameBeginInfo* frameBeginInfo);
XrResult xrEndFrame(XrSession session, const XrFrameEndInfo* frameEndInfo);
XrResult xrLocateViews(XrSession session, const XrViewLocateInfo* viewLocateInfo, XrViewState* viewState, uint32_t viewCapacityInput, uint32_t* viewCountOutput, XrView* views);
}
