#pragma once

#include <cstdint>
#include <memory>

#ifdef MIRAGEBRIDGE_ENABLE_GVR
#include "vr/gvr/capi/include/gvr.h"
#include "vr/gvr/capi/include/gvr_controller.h"
#endif

#include "miragebridge_protocol.h"

namespace miragebridge {

class GvrTrackingAdapter {
public:
    bool Initialize(void* gvrContext);
    void Shutdown();
    bool Poll(XRPacket* outPacket);

private:
    uint64_t frameId_ = 0;
    bool haveLastPose_ = false;
    uint64_t lastPoseNs_ = 0;
    float lastPos_[3] = {0.0f, 0.0f, 0.0f};
    float lastRot_[4] = {0.0f, 0.0f, 0.0f, 1.0f};
#ifdef MIRAGEBRIDGE_ENABLE_GVR
    gvr::Mat4f PerspectiveMatrixFromView(const gvr::Rectf& fov, float zNear, float zFar) const;
    void FillControllerPackets(XRPacket* outPacket);
    std::unique_ptr<gvr::GvrApi> gvrApi_;
    std::unique_ptr<gvr::BufferViewportList> viewportList_;
    std::unique_ptr<gvr::ControllerApi> controllerApi_;
    gvr::ControllerState controllerState_;
#endif
};

}
