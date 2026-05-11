#include "core/gvr_tracking_adapter.h"

#include <android/log.h>
#include <chrono>
#include <cmath>

#define MB_LOG_TAG "MirageBridgeTrack"
#define MB_LOGI(...) __android_log_print(ANDROID_LOG_INFO, MB_LOG_TAG, __VA_ARGS__)

namespace miragebridge {

namespace {
constexpr uint64_t kPredictionNs = 13888888ULL;

uint64_t MonotonicNs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
}

#ifdef MIRAGEBRIDGE_ENABLE_GVR
gvr::Mat4f GvrTrackingAdapter::PerspectiveMatrixFromView(const gvr::Rectf& fov, float zNear, float zFar) const {
    const float xLeft = -std::tan(fov.left * static_cast<float>(M_PI) / 180.0f) * zNear;
    const float xRight = std::tan(fov.right * static_cast<float>(M_PI) / 180.0f) * zNear;
    const float yBottom = -std::tan(fov.bottom * static_cast<float>(M_PI) / 180.0f) * zNear;
    const float yTop = std::tan(fov.top * static_cast<float>(M_PI) / 180.0f) * zNear;

    const float X = (2.0f * zNear) / (xRight - xLeft);
    const float Y = (2.0f * zNear) / (yTop - yBottom);
    const float A = (xRight + xLeft) / (xRight - xLeft);
    const float B = (yTop + yBottom) / (yTop - yBottom);
    const float C = (zNear + zFar) / (zNear - zFar);
    const float D = (2.0f * zNear * zFar) / (zNear - zFar);

    gvr::Mat4f out{};
    out.m[0][0] = X;
    out.m[0][2] = A;
    out.m[1][1] = Y;
    out.m[1][2] = B;
    out.m[2][2] = C;
    out.m[2][3] = D;
    out.m[3][2] = -1.0f;
    return out;
}

void GvrTrackingAdapter::FillControllerPackets(XRPacket* outPacket) {
    outPacket->controllerCount = 0;
    if (!controllerApi_) {
        return;
    }

    controllerState_.Update(*controllerApi_);
    if (controllerState_.GetConnectionState() != GVR_CONTROLLER_CONNECTED) {
        return;
    }

    ControllerPacket& c = outPacket->controllers[0];
    c.id = 0;
    c.buttons = 0;
    if (controllerState_.GetButtonState(GVR_CONTROLLER_BUTTON_CLICK)) c.buttons |= 1u << 0;
    if (controllerState_.GetButtonState(GVR_CONTROLLER_BUTTON_APP)) c.buttons |= 1u << 1;
    if (controllerState_.GetButtonState(GVR_CONTROLLER_BUTTON_HOME)) c.buttons |= 1u << 2;
    if (controllerState_.GetButtonState(GVR_CONTROLLER_BUTTON_TRIGGER)) c.buttons |= 1u << 3;
    if (controllerState_.GetButtonState(GVR_CONTROLLER_BUTTON_GRIP)) c.buttons |= 1u << 4;

    c.trigger = controllerState_.GetButtonState(GVR_CONTROLLER_BUTTON_TRIGGER) ? 1.0f : 0.0f;
    const gvr::Vec2f touch = controllerState_.GetTouchPos();
    c.joystick[0] = touch.x;
    c.joystick[1] = touch.y;

    const gvr::Vec3f pos = controllerState_.GetPosition();
    c.position[0] = pos.x;
    c.position[1] = pos.y;
    c.position[2] = pos.z;

    const gvr::ControllerQuat q = controllerState_.GetOrientation();
    c.rotation[0] = q.qx;
    c.rotation[1] = q.qy;
    c.rotation[2] = q.qz;
    c.rotation[3] = q.qw;

    outPacket->controllerCount = 1;
}
#endif

bool GvrTrackingAdapter::Initialize(void* gvrContext) {
#ifdef MIRAGEBRIDGE_ENABLE_GVR
    gvr_context* context = reinterpret_cast<gvr_context*>(gvrContext);
    if (context) {
        gvrApi_ = gvr::GvrApi::WrapNonOwned(context);
    }
    if (gvrApi_) {
        viewportList_.reset(new gvr::BufferViewportList(gvrApi_->CreateEmptyBufferViewportList()));
        controllerApi_.reset(new gvr::ControllerApi());
        if (!controllerApi_->Init(gvr::ControllerApi::DefaultOptions() | GVR_CONTROLLER_ENABLE_GYRO, gvrApi_->cobj())) {
            controllerApi_.reset();
        } else {
            controllerApi_->Resume();
        }
    }
#endif
    MB_LOGI("GvrTrackingAdapter initialized");
    return true;
}

void GvrTrackingAdapter::Shutdown() {
#ifdef MIRAGEBRIDGE_ENABLE_GVR
    if (controllerApi_) {
        controllerApi_->Pause();
    }
    controllerApi_.reset();
    viewportList_.reset();
    gvrApi_.reset();
#endif
    MB_LOGI("GvrTrackingAdapter shutdown");
}

bool GvrTrackingAdapter::Poll(XRPacket* outPacket) {
    if (!outPacket) {
        return false;
    }

    outPacket->magic = kProtocolMagic;
    outPacket->version = kProtocolVersion;
    outPacket->frameId = frameId_++;
    outPacket->monotonicNs = MonotonicNs();
    outPacket->predictedDisplayNs = outPacket->monotonicNs + kPredictionNs;
    outPacket->timestampSec = static_cast<double>(outPacket->monotonicNs) * 1e-9;

#ifdef MIRAGEBRIDGE_ENABLE_GVR
    if (gvrApi_) {
        gvr::ClockTimePoint target = gvr::GvrApi::GetTimePointNow();
        target.monotonic_system_time_nanos += kPredictionNs;
        const gvr::Mat4f head = gvrApi_->GetHeadSpaceFromStartSpaceTransform(target);

        outPacket->pos[0] = head.m[0][3];
        outPacket->pos[1] = head.m[1][3];
        outPacket->pos[2] = head.m[2][3];
        outPacket->rot[0] = 0.0f;
        outPacket->rot[1] = 0.0f;
        outPacket->rot[2] = 0.0f;
        outPacket->rot[3] = 1.0f;

        if (viewportList_) {
            viewportList_->SetToRecommendedBufferViewports();
        }

        for (uint32_t eye = 0; eye < kMaxEyes; ++eye) {
            const gvr::Eye gvrEye = eye == 0 ? GVR_LEFT_EYE : GVR_RIGHT_EYE;
            const gvr::Mat4f eyeFromHead = gvrApi_->GetEyeFromHeadMatrix(gvrEye);
            gvr::Mat4f proj{};
            if (viewportList_) {
                gvr::BufferViewport vp = gvrApi_->CreateBufferViewport();
                viewportList_->GetBufferViewport(static_cast<int>(eye), &vp);
                const gvr_rectf fov = vp.GetSourceFov();
                proj = PerspectiveMatrixFromView(fov, 0.1f, 100.0f);
            } else {
                for (int i = 0; i < 16; ++i) {
                    proj.m[i / 4][i % 4] = (i % 5 == 0) ? 1.0f : 0.0f;
                }
            }
            for (int i = 0; i < 16; ++i) {
                outPacket->eyes[eye].view[i] = eyeFromHead.m[i / 4][i % 4];
                outPacket->eyes[eye].proj[i] = proj.m[i / 4][i % 4];
            }
        }

        outPacket->angularVel[0] = 0.0f;
        outPacket->angularVel[1] = 0.0f;
        outPacket->angularVel[2] = 0.0f;
        outPacket->linearVel[0] = 0.0f;
        outPacket->linearVel[1] = 0.0f;
        outPacket->linearVel[2] = 0.0f;
        outPacket->displayWidth = 2560;
        outPacket->displayHeight = 1440;
        outPacket->displayHz = 72;
        outPacket->trackingHz = 200;
        FillControllerPackets(outPacket);
        return true;
    }
#endif

    const double t = static_cast<double>(MonotonicNs()) * 1e-9;
    const float yaw = static_cast<float>(std::sin(t * 0.3) * 0.15);
    const float halfYaw = yaw * 0.5f;

    outPacket->pos[0] = static_cast<float>(std::sin(t) * 0.02);
    outPacket->pos[1] = 1.65f;
    outPacket->pos[2] = static_cast<float>(std::cos(t) * 0.02);
    outPacket->rot[0] = 0.0f;
    outPacket->rot[1] = std::sin(halfYaw);
    outPacket->rot[2] = 0.0f;
    outPacket->rot[3] = std::cos(halfYaw);
    outPacket->angularVel[0] = 0.0f;
    outPacket->angularVel[1] = 0.3f;
    outPacket->angularVel[2] = 0.0f;
    outPacket->linearVel[0] = 0.02f;
    outPacket->linearVel[1] = 0.0f;
    outPacket->linearVel[2] = 0.02f;

    for (uint32_t eye = 0; eye < kMaxEyes; ++eye) {
        for (int i = 0; i < 16; ++i) {
            outPacket->eyes[eye].view[i] = (i % 5 == 0) ? 1.0f : 0.0f;
            outPacket->eyes[eye].proj[i] = (i % 5 == 0) ? 1.0f : 0.0f;
        }
        outPacket->eyes[eye].view[12] = eye == 0 ? -0.032f : 0.032f;
    }

    outPacket->controllerCount = 0;
    outPacket->displayWidth = 2560;
    outPacket->displayHeight = 1440;
    outPacket->displayHz = 72;
    outPacket->trackingHz = 200;
    return true;
}

}
