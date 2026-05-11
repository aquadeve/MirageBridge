#pragma once

#include <atomic>
#include <cstdint>

namespace miragebridge {

constexpr uint32_t kProtocolMagic = 0x4D425247;
constexpr uint32_t kProtocolVersion = 1;
constexpr uint32_t kMaxEyes = 2;
constexpr uint32_t kMaxControllers = 2;
constexpr uint32_t kSbsWidth = 2048;
constexpr uint32_t kSbsHeight = 1024;
constexpr uint32_t kSbsBytes = kSbsWidth * kSbsHeight * 4;

constexpr uint32_t kSocketChunkMagic = 0x4D425343;
constexpr uint32_t kSocketChunkTracking = 1;
constexpr uint32_t kSocketChunkFrame = 2;
constexpr uint32_t kSocketChunkFrameData = 3;

constexpr uint32_t kSocketFrameChunkBytes = 60000;

#pragma pack(push, 1)
struct EyePacket {
    float view[16];
    float proj[16];
};

struct ControllerPacket {
    uint32_t id;
    uint32_t buttons;
    float trigger;
    float joystick[2];
    float position[3];
    float rotation[4];
};

struct XRPacket {
    uint32_t magic;
    uint32_t version;
    uint64_t frameId;
    uint64_t monotonicNs;
    uint64_t predictedDisplayNs;
    double timestampSec;
    float pos[3];
    float rot[4];
    float angularVel[3];
    float linearVel[3];
    EyePacket eyes[kMaxEyes];
    ControllerPacket controllers[kMaxControllers];
    uint32_t controllerCount;
    uint32_t displayWidth;
    uint32_t displayHeight;
    uint32_t displayHz;
    uint32_t trackingHz;
};

struct SBSFrameHeader {
    uint32_t magic;
    uint32_t version;
    uint64_t frameId;
    uint64_t monotonicNs;
    uint32_t sbsWidth;
    uint32_t sbsHeight;
    uint32_t strideBytes;
    uint32_t format;
    uint32_t payloadBytes;
};

struct SBSFramePacket {
    SBSFrameHeader header;
    uint8_t payload[kSbsBytes];
};

struct SocketChunkHeader {
    uint32_t magic;
    uint32_t type;
    uint32_t size;
};

struct SocketFrameDataHeader {
    uint64_t frameId;
    uint32_t payloadOffset;
    uint32_t payloadSize;
};

struct RingHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t slotCount;
    uint32_t slotSize;
    std::atomic<uint64_t> writeSeq;
    std::atomic<uint64_t> readSeq;
};
#pragma pack(pop)

struct TransportConfig {
    const char* trackingName;
    const char* frameName;
    uint32_t trackingSlots;
    uint32_t frameSlots;
};

inline TransportConfig DefaultConfig() {
    return {"/miragebridge_tracking", "/miragebridge_frames", 512, 8};
}

}
