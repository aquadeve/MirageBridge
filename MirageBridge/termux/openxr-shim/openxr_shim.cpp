#include "openxr_shim.h"

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
}

extern "C" XrResult xrWaitFrame(XrSession, const XrFrameWaitInfo*, XrFrameState* frameState) {
    EnsureReader();
    uint64_t seq = 0;
    g_reader.ReadLatest(&g_last, sizeof(g_last), &seq);
    if (frameState) {
        frameState->predictedDisplayTime = static_cast<XrTime>(g_last.predictedDisplayNs);
        frameState->shouldRender = 1;
    }
    return 0;
}

extern "C" XrResult xrBeginFrame(XrSession, const XrFrameBeginInfo*) {
    return 0;
}

extern "C" XrResult xrEndFrame(XrSession, const XrFrameEndInfo*) {
    return 0;
}

extern "C" XrResult xrLocateViews(XrSession, const XrViewLocateInfo*, XrViewState* viewState, uint32_t viewCapacityInput, uint32_t* viewCountOutput, XrView* views) {
    EnsureReader();
    uint64_t seq = 0;
    g_reader.ReadLatest(&g_last, sizeof(g_last), &seq);

    if (viewState) {
        viewState->viewStateFlags = 1;
    }

    const uint32_t need = 2;
    if (viewCountOutput) {
        *viewCountOutput = need;
    }
    if (!views || viewCapacityInput < need) {
        return 0;
    }

    for (uint32_t i = 0; i < 2; ++i) {
        views[i].posePosition[0] = g_last.pos[0] + (i == 0 ? -0.032f : 0.032f);
        views[i].posePosition[1] = g_last.pos[1];
        views[i].posePosition[2] = g_last.pos[2];
        views[i].poseOrientation[0] = g_last.rot[0];
        views[i].poseOrientation[1] = g_last.rot[1];
        views[i].poseOrientation[2] = g_last.rot[2];
        views[i].poseOrientation[3] = g_last.rot[3];
        views[i].fov[0] = g_last.eyes[i].proj[8] - 1.0f;
        views[i].fov[1] = 1.0f - g_last.eyes[i].proj[8];
        views[i].fov[2] = 1.0f;
        views[i].fov[3] = -1.0f;
    }

    return 0;
}
