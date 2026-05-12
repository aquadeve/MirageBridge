#include "core/gvr_tracking_adapter.h"

#include <android/log.h>
#include <chrono>
#include <cmath>
#include <cstring>

#define MB_LOG_TAG "MirageBridgeTrack"
#define MB_LOGI(...) __android_log_print(ANDROID_LOG_INFO, MB_LOG_TAG, __VA_ARGS__)

namespace miragebridge {

namespace {
constexpr uint64_t kPredictionNs = 13888888ULL;
constexpr float kDegToRad = static_cast<float>(M_PI) / 180.0f;

uint64_t MonotonicNs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

void Identity(float out[16]) {
    for (int i = 0; i < 16; ++i) {
        out[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    }
}

void CopyMat(const float in[4][4], float out[16]) {
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            out[r * 4 + c] = in[r][c];
        }
    }
}

#ifdef MIRAGEBRIDGE_ENABLE_GVR
void CopyMat(const gvr::Mat4f& in, float out[16]) {
    CopyMat(in.m, out);
}
#endif

void InvertRigidRowMajor(const float in[4][4], float out[16], float outPos[3]) {
    Identity(out);
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            out[r * 4 + c] = in[c][r];
        }
    }

    const float t[3] = {in[0][3], in[1][3], in[2][3]};
    for (int r = 0; r < 3; ++r) {
        outPos[r] = -(out[r * 4 + 0] * t[0] + out[r * 4 + 1] * t[1] + out[r * 4 + 2] * t[2]);
        out[r * 4 + 3] = outPos[r];
    }
}

void QuaternionFromRowMajor3x3(const float m[16], float q[4]) {
    const float trace = m[0] + m[5] + m[10];
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q[3] = 0.25f * s;
        q[0] = (m[9] - m[6]) / s;
        q[1] = (m[2] - m[8]) / s;
        q[2] = (m[4] - m[1]) / s;
    } else if (m[0] > m[5] && m[0] > m[10]) {
        const float s = std::sqrt(1.0f + m[0] - m[5] - m[10]) * 2.0f;
        q[3] = (m[9] - m[6]) / s;
        q[0] = 0.25f * s;
        q[1] = (m[1] + m[4]) / s;
        q[2] = (m[2] + m[8]) / s;
    } else if (m[5] > m[10]) {
        const float s = std::sqrt(1.0f + m[5] - m[0] - m[10]) * 2.0f;
        q[3] = (m[2] - m[8]) / s;
        q[0] = (m[1] + m[4]) / s;
        q[1] = 0.25f * s;
        q[2] = (m[6] + m[9]) / s;
    } else {
        const float s = std::sqrt(1.0f + m[10] - m[0] - m[5]) * 2.0f;
        q[3] = (m[4] - m[1]) / s;
        q[0] = (m[2] + m[8]) / s;
        q[1] = (m[6] + m[9]) / s;
        q[2] = 0.25f * s;
    }

    const float len = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (len > 0.0f) {
        q[0] /= len;
        q[1] /= len;
        q[2] /= len;
        q[3] /= len;
    }
}

void EstimateVelocities(uint64_t nowNs,
                        const float pos[3],
                        const float rot[4],
                        bool* haveLastPose,
                        uint64_t* lastNs,
                        float lastPos[3],
                        float lastRot[4],
                        float linearVel[3],
                        float angularVel[3]) {
    linearVel[0] = linearVel[1] = linearVel[2] = 0.0f;
    angularVel[0] = angularVel[1] = angularVel[2] = 0.0f;

    if (*haveLastPose && nowNs > *lastNs) {
        const float dt = static_cast<float>(static_cast<double>(nowNs - *lastNs) * 1e-9);
        if (dt > 0.0f && dt < 1.0f) {
            for (int i = 0; i < 3; ++i) {
                linearVel[i] = (pos[i] - lastPos[i]) / dt;
            }

            float prev[4] = {lastRot[0], lastRot[1], lastRot[2], lastRot[3]};
            if (prev[0] * rot[0] + prev[1] * rot[1] + prev[2] * rot[2] + prev[3] * rot[3] < 0.0f) {
                prev[0] = -prev[0];
                prev[1] = -prev[1];
                prev[2] = -prev[2];
                prev[3] = -prev[3];
            }

            const float dq[4] = {
                rot[3] * -prev[0] + rot[0] * prev[3] + rot[1] * -prev[2] - rot[2] * -prev[1],
                rot[3] * -prev[1] - rot[0] * -prev[2] + rot[1] * prev[3] + rot[2] * -prev[0],
                rot[3] * -prev[2] + rot[0] * -prev[1] - rot[1] * -prev[0] + rot[2] * prev[3],
                rot[3] * prev[3] - rot[0] * -prev[0] - rot[1] * -prev[1] - rot[2] * -prev[2],
            };
            angularVel[0] = 2.0f * dq[0] / dt;
            angularVel[1] = 2.0f * dq[1] / dt;
            angularVel[2] = 2.0f * dq[2] / dt;
        }
    }

    *haveLastPose = true;
    *lastNs = nowNs;
    std::memcpy(lastPos, pos, sizeof(float) * 3);
    std::memcpy(lastRot, rot, sizeof(float) * 4);
}
}

#ifdef MIRAGEBRIDGE_ENABLE_GVR
gvr::Mat4f GvrTrackingAdapter::PerspectiveMatrixFromView(const gvr::Rectf& fov, float zNear, float zFar) const {
    const float xLeft = -std::tan(fov.left * kDegToRad) * zNear;
    const float xRight = std::tan(fov.right * kDegToRad) * zNear;
    const float yBottom = -std::tan(fov.bottom * kDegToRad) * zNear;
    const float yTop = std::tan(fov.top * kDegToRad) * zNear;

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
        const int32_t options = gvr::ControllerApi::DefaultOptions()
            | GVR_CONTROLLER_ENABLE_GYRO
            | GVR_CONTROLLER_ENABLE_ACCEL
            | GVR_CONTROLLER_ENABLE_POSITION
            | GVR_CONTROLLER_ENABLE_ARM_MODEL;
        if (!controllerApi_->Init(options, gvrApi_->cobj())) {
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
        float startFromHead[16]{};
        InvertRigidRowMajor(head.m, startFromHead, outPacket->pos);
        QuaternionFromRowMajor3x3(startFromHead, outPacket->rot);

        gvr::Value floorHeight;
        if (gvrApi_->GetCurrentProperties().Get(GVR_PROPERTY_TRACKING_FLOOR_HEIGHT, &floorHeight)) {
            outPacket->pos[1] += floorHeight.f;
        }

        if (viewportList_) {
            viewportList_->SetToRecommendedBufferViewports();
        }

        for (uint32_t eye = 0; eye < kMaxEyes; ++eye) {
            const gvr::Eye gvrEye = eye == 0 ? GVR_LEFT_EYE : GVR_RIGHT_EYE;
            const gvr::Mat4f eyeFromHead = gvrApi_->GetEyeFromHeadMatrix(gvrEye);
            gvr::Mat4f proj{};
            gvr::Rectf fov{};
            if (viewportList_) {
                gvr::BufferViewport vp = gvrApi_->CreateBufferViewport();
                viewportList_->GetBufferViewport(static_cast<int>(eye), &vp);
                fov = vp.GetSourceFov();
                proj = PerspectiveMatrixFromView(fov, 0.1f, 100.0f);
            } else {
                for (int i = 0; i < 16; ++i) {
                    proj.m[i / 4][i % 4] = (i % 5 == 0) ? 1.0f : 0.0f;
                }
                fov = {43.0f, 43.0f, 43.0f, 43.0f};
            }
            CopyMat(eyeFromHead, outPacket->eyes[eye].view);
            CopyMat(proj, outPacket->eyes[eye].proj);
            outPacket->eyes[eye].fov[0] = -fov.left * kDegToRad;
            outPacket->eyes[eye].fov[1] = fov.right * kDegToRad;
            outPacket->eyes[eye].fov[2] = fov.top * kDegToRad;
            outPacket->eyes[eye].fov[3] = -fov.bottom * kDegToRad;
        }

        EstimateVelocities(outPacket->monotonicNs,
                           outPacket->pos,
                           outPacket->rot,
                           &haveLastPose_,
                           &lastPoseNs_,
                           lastPos_,
                           lastRot_,
                           outPacket->linearVel,
                           outPacket->angularVel);
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
        outPacket->eyes[eye].view[3] = eye == 0 ? -0.032f : 0.032f;
        outPacket->eyes[eye].fov[0] = -0.75f;
        outPacket->eyes[eye].fov[1] = 0.75f;
        outPacket->eyes[eye].fov[2] = 0.75f;
        outPacket->eyes[eye].fov[3] = -0.75f;
    }

    outPacket->controllerCount = 0;
    outPacket->displayWidth = 2560;
    outPacket->displayHeight = 1440;
    outPacket->displayHz = 72;
    outPacket->trackingHz = 200;
    return true;
}

}
