#include "mirage_runtime.h"

#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

#include "miragebridge_protocol.h"
#include "transport_reader.h"
#include "transport_writer.h"

namespace {

using miragebridge::AudioPacket;
using miragebridge::DefaultConfig;
using miragebridge::SBSFramePacket;
using miragebridge::XRPacket;

uint64_t MonotonicNs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

void FillVec3(const float in[3], mbr_vec3* out) {
    out->x = in[0];
    out->y = in[1];
    out->z = in[2];
}

void FillQuat(const float in[4], mbr_quat* out) {
    out->x = in[0];
    out->y = in[1];
    out->z = in[2];
    out->w = in[3];
}

void FillPose(const XRPacket& packet, mbr_pose* out) {
    FillVec3(packet.pos, &out->position);
    FillQuat(packet.rot, &out->rotation);
    FillVec3(packet.linearVel, &out->linear_velocity);
    FillVec3(packet.angularVel, &out->angular_velocity);
    out->timestamp_ns = packet.monotonicNs;
    out->predicted_display_ns = packet.predictedDisplayNs;
}

void FillHeadset(const XRPacket& packet, mbr_headset_state* out) {
    out->frame_id = packet.frameId;
    FillPose(packet, &out->pose);
    for (uint32_t eye = 0; eye < 2; ++eye) {
        std::memcpy(out->eyes[eye].view, packet.eyes[eye].view, sizeof(out->eyes[eye].view));
        std::memcpy(out->eyes[eye].projection, packet.eyes[eye].proj, sizeof(out->eyes[eye].projection));
        std::memcpy(out->eyes[eye].fov, packet.eyes[eye].fov, sizeof(out->eyes[eye].fov));
    }
    out->display_width = packet.displayWidth;
    out->display_height = packet.displayHeight;
    out->display_hz = packet.displayHz;
    out->tracking_hz = packet.trackingHz;
}

void FillController(const miragebridge::ControllerPacket& packet, mbr_controller_state* out) {
    out->id = packet.id;
    out->connected = 1;
    out->buttons = packet.buttons;
    out->trigger = packet.trigger;
    out->touchpad.x = packet.joystick[0];
    out->touchpad.y = packet.joystick[1];
    FillVec3(packet.position, &out->pose.position);
    FillQuat(packet.rotation, &out->pose.rotation);
    out->pose.linear_velocity = {};
    out->pose.angular_velocity = {};
    out->pose.timestamp_ns = MonotonicNs();
    out->pose.predicted_display_ns = out->pose.timestamp_ns;
}

} 

struct mbr_runtime {
    miragebridge::RingReader tracking;
    miragebridge::RingReader frames;
    miragebridge::RingWriter clientFrames;
    miragebridge::RingWriter audioOut;

    bool connected = false;
    bool havePose = false;
    bool haveFrame = false;
    uint64_t lastTrackingSeq = UINT64_MAX;
    uint64_t lastFrameSeq = UINT64_MAX;
    XRPacket lastPacket{};
    SBSFramePacket lastFrameHeaderScratch{};
    std::unique_ptr<SBSFramePacket> submitScratch;
    std::unique_ptr<SBSFramePacket> readFrameScratch;
    std::unique_ptr<AudioPacket> audioScratch;
    mbr_event events[128]{};
    uint32_t eventRead = 0;
    uint32_t eventWrite = 0;
    uint64_t nextEventId = 1;
    mbr_metrics metrics{};

    void PushEvent(uint32_t type, uint32_t code = 0, uint64_t value = 0) {
        const uint32_t next = (eventWrite + 1) % 128;
        if (next == eventRead) {
            metrics.dropped_events++;
            return;
        }
        mbr_event& ev = events[eventWrite];
        ev = {};
        ev.id = nextEventId++;
        ev.timestamp_ns = MonotonicNs();
        ev.type = type;
        ev.code = code;
        ev.value = value;
        eventWrite = next;
    }
};

extern "C" {

MBR_API const char* mbr_result_to_string(mbr_result result) {
    switch (result) {
        case MBR_SUCCESS: return "success";
        case MBR_ERROR_INVALID_ARGUMENT: return "invalid argument";
        case MBR_ERROR_NOT_CONNECTED: return "not connected";
        case MBR_ERROR_TRANSPORT_UNAVAILABLE: return "transport unavailable";
        case MBR_ERROR_TIMEOUT: return "timeout";
        case MBR_ERROR_UNSUPPORTED: return "unsupported";
        case MBR_ERROR_BUFFER_TOO_SMALL: return "buffer too small";
        case MBR_ERROR_INTERNAL: return "internal error";
        default: return "unknown result";
    }
}

MBR_API mbr_result mbr_runtime_create(const mbr_runtime_config* config, mbr_runtime** out_runtime) {
    if (!out_runtime) {
        return MBR_ERROR_INVALID_ARGUMENT;
    }
    auto runtime = std::make_unique<mbr_runtime>();
    runtime->submitScratch = std::make_unique<SBSFramePacket>();
    runtime->readFrameScratch = std::make_unique<SBSFramePacket>();
    runtime->audioScratch = std::make_unique<AudioPacket>();
    if (config && config->endpoint) {
        mbr_result result = mbr_runtime_connect(runtime.get(), config->endpoint);
        if (result != MBR_SUCCESS) {
            return result;
        }
    }
    *out_runtime = runtime.release();
    return MBR_SUCCESS;
}

MBR_API void mbr_runtime_destroy(mbr_runtime* runtime) {
    if (!runtime) {
        return;
    }
    mbr_runtime_disconnect(runtime);
    delete runtime;
}

MBR_API mbr_result mbr_runtime_connect(mbr_runtime* runtime, const char* endpoint) {
    if (!runtime) {
        return MBR_ERROR_INVALID_ARGUMENT;
    }
    const std::string target = endpoint ? endpoint : "local";
    if (target != "local" && target != "localhost" && target != "shm") {
        return MBR_ERROR_UNSUPPORTED;
    }

    const auto cfg = DefaultConfig();
    const bool trackingOk = runtime->tracking.Open(cfg.trackingName, sizeof(XRPacket));
    const bool framesOk = runtime->frames.Open(cfg.frameName, sizeof(SBSFramePacket));
    if (!trackingOk) {
        runtime->tracking.Close();
        runtime->frames.Close();
        return MBR_ERROR_TRANSPORT_UNAVAILABLE;
    }

    runtime->clientFrames.Create(cfg.clientFrameName, cfg.clientFrameSlots, sizeof(SBSFramePacket));
    runtime->audioOut.Create(cfg.audioOutName, cfg.audioSlots, sizeof(AudioPacket));
    runtime->connected = true;
    runtime->havePose = false;
    runtime->haveFrame = false;
    runtime->lastTrackingSeq = UINT64_MAX;
    runtime->lastFrameSeq = UINT64_MAX;
    runtime->PushEvent(MBR_EVENT_CONNECTED);

    if (!framesOk) {
        runtime->PushEvent(MBR_EVENT_TRANSPORT_WARNING, 1);
    }
    return MBR_SUCCESS;
}

MBR_API void mbr_runtime_disconnect(mbr_runtime* runtime) {
    if (!runtime) {
        return;
    }
    if (runtime->connected) {
        runtime->PushEvent(MBR_EVENT_DISCONNECTED);
    }
    runtime->tracking.Close();
    runtime->frames.Close();
    runtime->clientFrames.Close();
    runtime->audioOut.Close();
    runtime->connected = false;
    runtime->havePose = false;
    runtime->haveFrame = false;
}

MBR_API uint32_t mbr_runtime_is_connected(const mbr_runtime* runtime) {
    return runtime && runtime->connected ? 1u : 0u;
}

MBR_API mbr_result mbr_runtime_poll_events(mbr_runtime* runtime, mbr_event* events, uint32_t capacity, uint32_t* out_count) {
    if (!runtime || (!events && capacity > 0)) {
        return MBR_ERROR_INVALID_ARGUMENT;
    }

    uint32_t count = 0;
    while (count < capacity && runtime->eventRead != runtime->eventWrite) {
        events[count++] = runtime->events[runtime->eventRead];
        runtime->eventRead = (runtime->eventRead + 1) % 128;
    }
    if (out_count) {
        *out_count = count;
    }
    return MBR_SUCCESS;
}

MBR_API mbr_result mbr_runtime_wait_frame(mbr_runtime* runtime, uint64_t timeout_ns, mbr_frame_timing* out_timing) {
    if (!runtime || !out_timing) {
        return MBR_ERROR_INVALID_ARGUMENT;
    }
    if (!runtime->connected) {
        return MBR_ERROR_NOT_CONNECTED;
    }

    const uint64_t start = MonotonicNs();
    for (;;) {
        uint64_t seq = 0;
        XRPacket packet{};
        if (runtime->tracking.ReadLatest(&packet, sizeof(packet), &seq) && packet.magic == miragebridge::kProtocolMagic) {
            runtime->lastPacket = packet;
            runtime->havePose = true;
            runtime->metrics.pose_packets_read++;
            runtime->metrics.last_pose_ns = packet.monotonicNs;
            if (seq != runtime->lastTrackingSeq) {
                runtime->lastTrackingSeq = seq;
                runtime->PushEvent(MBR_EVENT_POSE, 0, packet.frameId);
            }

            out_timing->frame_id = packet.frameId;
            out_timing->predicted_display_ns = packet.predictedDisplayNs;
            out_timing->display_period_ns = packet.displayHz ? 1000000000ull / packet.displayHz : 13888888ull;
            out_timing->should_render = 1;
            return MBR_SUCCESS;
        }

        if (timeout_ns == 0 || MonotonicNs() - start >= timeout_ns) {
            return MBR_ERROR_TIMEOUT;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(250));
    }
}

MBR_API mbr_result mbr_runtime_get_headset_state(mbr_runtime* runtime, mbr_headset_state* out_state) {
    if (!runtime || !out_state) {
        return MBR_ERROR_INVALID_ARGUMENT;
    }
    mbr_frame_timing timing{};
    if (!runtime->havePose) {
        mbr_result result = mbr_runtime_wait_frame(runtime, 0, &timing);
        if (result != MBR_SUCCESS) {
            return result;
        }
    }
    FillHeadset(runtime->lastPacket, out_state);
    return MBR_SUCCESS;
}

MBR_API mbr_result mbr_runtime_get_controller_state(mbr_runtime* runtime, uint32_t index, mbr_controller_state* out_state) {
    if (!runtime || !out_state) {
        return MBR_ERROR_INVALID_ARGUMENT;
    }
    if (!runtime->havePose) {
        mbr_frame_timing timing{};
        mbr_result result = mbr_runtime_wait_frame(runtime, 0, &timing);
        if (result != MBR_SUCCESS) {
            return result;
        }
    }
    *out_state = {};
    if (index >= runtime->lastPacket.controllerCount || index >= miragebridge::kMaxControllers) {
        out_state->id = index;
        out_state->connected = 0;
        return MBR_SUCCESS;
    }
    FillController(runtime->lastPacket.controllers[index], out_state);
    return MBR_SUCCESS;
}

MBR_API mbr_result mbr_runtime_get_latest_sbs_frame(mbr_runtime* runtime,
                                                    mbr_sbs_frame_desc* out_desc,
                                                    void* pixel_buffer,
                                                    uint32_t pixel_buffer_bytes,
                                                    uint32_t* out_bytes) {
    if (!runtime || !out_desc) {
        return MBR_ERROR_INVALID_ARGUMENT;
    }
    if (!runtime->connected) {
        return MBR_ERROR_NOT_CONNECTED;
    }

    uint64_t seq = 0;
    if (!runtime->frames.ReadLatest(runtime->readFrameScratch.get(), sizeof(SBSFramePacket), &seq)) {
        return MBR_ERROR_TIMEOUT;
    }
    const auto& packet = *runtime->readFrameScratch;
    const uint32_t bytes = packet.header.payloadBytes;
    if (out_bytes) {
        *out_bytes = bytes;
    }
    if (pixel_buffer && pixel_buffer_bytes < bytes) {
        return MBR_ERROR_BUFFER_TOO_SMALL;
    }
    if (pixel_buffer && bytes > 0) {
        std::memcpy(pixel_buffer, packet.payload, bytes);
    }
    out_desc->frame_id = packet.header.frameId;
    out_desc->target_display_ns = packet.header.targetDisplayNs ? packet.header.targetDisplayNs : packet.header.monotonicNs;
    out_desc->width = packet.header.sbsWidth;
    out_desc->height = packet.header.sbsHeight;
    out_desc->stride_bytes = packet.header.strideBytes;
    out_desc->pixel_format = MBR_PIXEL_FORMAT_RGBA8;
    out_desc->pixels = pixel_buffer;
    out_desc->bytes = bytes;
    runtime->metrics.frame_packets_read++;
    if (seq != runtime->lastFrameSeq) {
        runtime->lastFrameSeq = seq;
        runtime->PushEvent(MBR_EVENT_FRAME, 0, packet.header.frameId);
    }
    return MBR_SUCCESS;
}

MBR_API mbr_result mbr_runtime_submit_sbs_frame(mbr_runtime* runtime, const mbr_sbs_frame_desc* frame) {
    if (!runtime || !frame || !frame->pixels) {
        return MBR_ERROR_INVALID_ARGUMENT;
    }
    if (frame->pixel_format != MBR_PIXEL_FORMAT_RGBA8 || frame->width > miragebridge::kSbsWidth || frame->height > miragebridge::kSbsHeight) {
        return MBR_ERROR_UNSUPPORTED;
    }
    const uint32_t stride = frame->stride_bytes ? frame->stride_bytes : frame->width * 4;
    const uint32_t bytes = stride * frame->height;
    if (bytes > miragebridge::kSbsBytes || frame->bytes < bytes) {
        return MBR_ERROR_INVALID_ARGUMENT;
    }

    auto& packet = *runtime->submitScratch;
    packet.header.magic = miragebridge::kProtocolMagic;
    packet.header.version = miragebridge::kProtocolVersion;
    packet.header.frameId = frame->frame_id;
    packet.header.monotonicNs = MonotonicNs();
    packet.header.targetDisplayNs = frame->target_display_ns;
    packet.header.sbsWidth = frame->width;
    packet.header.sbsHeight = frame->height;
    packet.header.strideBytes = stride;
    packet.header.format = MBR_PIXEL_FORMAT_RGBA8;
    packet.header.payloadBytes = bytes;
    std::memcpy(packet.payload, frame->pixels, bytes);
    if (bytes < miragebridge::kSbsBytes) {
        std::memset(packet.payload + bytes, 0, miragebridge::kSbsBytes - bytes);
    }
    if (!runtime->clientFrames.Write(&packet, sizeof(packet))) {
        return MBR_ERROR_TRANSPORT_UNAVAILABLE;
    }
    runtime->metrics.frames_submitted++;
    runtime->metrics.last_submit_ns = packet.header.monotonicNs;
    return MBR_SUCCESS;
}

MBR_API mbr_result mbr_runtime_submit_audio(mbr_runtime* runtime, const mbr_audio_desc* audio) {
    if (!runtime || !audio || !audio->samples) {
        return MBR_ERROR_INVALID_ARGUMENT;
    }
    if (audio->channels > miragebridge::kAudioMaxChannels || audio->bytes > miragebridge::kAudioMaxBytes) {
        return MBR_ERROR_INVALID_ARGUMENT;
    }
    auto& packet = *runtime->audioScratch;
    packet.magic = miragebridge::kProtocolMagic;
    packet.version = miragebridge::kProtocolVersion;
    packet.packetId = audio->packet_id;
    packet.monotonicNs = audio->timestamp_ns ? audio->timestamp_ns : MonotonicNs();
    packet.sampleRate = audio->sample_rate;
    packet.channels = audio->channels;
    packet.frameCount = audio->frame_count;
    packet.format = audio->format;
    packet.payloadBytes = audio->bytes;
    std::memcpy(packet.payload, audio->samples, audio->bytes);
    if (!runtime->audioOut.Write(&packet, sizeof(packet))) {
        return MBR_ERROR_TRANSPORT_UNAVAILABLE;
    }
    runtime->metrics.audio_packets_submitted++;
    return MBR_SUCCESS;
}

MBR_API mbr_result mbr_runtime_get_metrics(const mbr_runtime* runtime, mbr_metrics* out_metrics) {
    if (!runtime || !out_metrics) {
        return MBR_ERROR_INVALID_ARGUMENT;
    }
    *out_metrics = runtime->metrics;
    return MBR_SUCCESS;
}

}
