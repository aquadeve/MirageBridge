#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace miragebridge {

constexpr uint32_t kProtocolMagic = 0x4D425247;
constexpr uint32_t kProtocolVersion = 3;
constexpr uint32_t kMaxEyes = 2;
constexpr uint32_t kMaxControllers = 2;
constexpr uint32_t kSbsWidth = 2048;
constexpr uint32_t kSbsHeight = 1024;
constexpr uint32_t kSbsBytes = kSbsWidth * kSbsHeight * 4;

constexpr uint32_t kSocketChunkMagic = 0x4D425343;
constexpr uint32_t kSocketChunkSharedMemory = 1;
constexpr uint32_t kSocketChunkTracking = 2;
constexpr uint32_t kSocketChunkFrame = 3;
constexpr uint32_t kSocketChunkClientFrame = 4;
constexpr uint32_t kSocketChunkAudio = 5;
constexpr uint32_t kSocketChunkInput = 6;
constexpr uint32_t kSocketChunkRuntimeEvent = 7;
constexpr uint32_t kSocketChunkDeviceConfig = 8;
constexpr uint32_t kSocketChunkFrameTiming = 9;
constexpr uint32_t kSocketChunkBufferControl = 10;
constexpr uint32_t kSocketChunkFrameAck = 11;

constexpr uint32_t kRingKindTracking = 1;
constexpr uint32_t kRingKindFrame = 2;
constexpr uint32_t kRingKindClientFrame = 3;
constexpr uint32_t kRingKindAudioOut = 4;
constexpr uint32_t kRingKindAudioIn = 5;
constexpr uint32_t kRingKindRuntimeEvent = 6;

constexpr const char* kAndroidControlSocketName = "miragebridge.termux";
constexpr uint32_t kSocketMaxPayload = 60 * 1024;
constexpr uint32_t kAudioMaxFrames = 960;
constexpr uint32_t kAudioMaxChannels = 2;
constexpr uint32_t kAudioMaxBytes = kAudioMaxFrames * kAudioMaxChannels * sizeof(float);

enum RuntimeEventType : uint32_t {
    kRuntimeEventNone = 0,
    kRuntimeEventConnected = 1,
    kRuntimeEventDisconnected = 2,
    kRuntimeEventPose = 3,
    kRuntimeEventFrame = 4,
    kRuntimeEventController = 5,
    kRuntimeEventAudioUnderrun = 6,
    kRuntimeEventTransportWarning = 7,
};

enum RuntimeCommandType : uint32_t {
    kRuntimeCommandNone = 0,
    kRuntimeCommandSubmitSbsFrame = 1,
    kRuntimeCommandSubmitEncodedVideo = 2,
    kRuntimeCommandSubmitAudio = 3,
    kRuntimeCommandHapticPulse = 4,
    kRuntimeCommandSetBitrate = 5,
    kRuntimeCommandSetFramePacing = 6,
    kRuntimeCommandCreateBuffer = 7,
    kRuntimeCommandDestroyBuffer = 8,
    kRuntimeCommandSubmitBuffer = 9,
};

enum BufferMemoryType : uint32_t {
    kBufferMemorySocketPayload = 0,
    kBufferMemoryMmapFd = 1,
    kBufferMemoryAshmemFd = 2,
    kBufferMemoryAHardwareBuffer = 3,
};

enum BufferPixelFormat : uint32_t {
    kBufferPixelFormatRgba8 = 1,
    kBufferPixelFormatBgra8 = 2,
    kBufferPixelFormatRgbx8 = 3,
};

#pragma pack(push, 1)
struct EyePacket {
    float view[16];
    float proj[16];
    float fov[4];
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
    uint64_t targetDisplayNs;
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

struct EncodedVideoPacket {
    uint32_t magic;
    uint32_t version;
    uint64_t frameId;
    uint64_t captureNs;
    uint64_t targetDisplayNs;
    uint32_t codec;
    uint32_t flags;
    uint32_t width;
    uint32_t height;
    uint32_t payloadBytes;
};

struct AudioPacket {
    uint32_t magic;
    uint32_t version;
    uint64_t packetId;
    uint64_t monotonicNs;
    uint32_t sampleRate;
    uint32_t channels;
    uint32_t frameCount;
    uint32_t format;
    uint32_t payloadBytes;
    uint8_t payload[kAudioMaxBytes];
};

struct RuntimeEventPacket {
    uint32_t magic;
    uint32_t version;
    uint64_t eventId;
    uint64_t monotonicNs;
    uint32_t type;
    uint32_t code;
    uint64_t value;
    float data[8];
};

struct RuntimeCommandPacket {
    uint32_t magic;
    uint32_t version;
    uint64_t commandId;
    uint64_t monotonicNs;
    uint32_t type;
    uint32_t payloadBytes;
};

struct DeviceConfigPacket {
    uint32_t magic;
    uint32_t version;
    uint64_t monotonicNs;
    uint32_t displayWidth;
    uint32_t displayHeight;
    uint32_t displayHz;
    uint32_t eyeWidth;
    uint32_t eyeHeight;
    float ipdMeters;
    float fov[kMaxEyes][4];
    uint32_t maxFramesInFlight;
    uint32_t preferredMemoryType;
};

struct FrameTimingPacket {
    uint32_t magic;
    uint32_t version;
    uint64_t frameId;
    uint64_t monotonicNs;
    uint64_t predictedDisplayNs;
    uint64_t displayPeriodNs;
    uint32_t maxFramesInFlight;
    uint32_t queuedFrameCount;
};

struct BufferControlPacket {
    uint32_t magic;
    uint32_t version;
    uint64_t commandId;
    uint64_t bufferId;
    uint32_t commandType;
    uint32_t memoryType;
    uint32_t width;
    uint32_t height;
    uint32_t strideBytes;
    uint32_t format;
    uint32_t payloadBytes;
};

struct FrameAckPacket {
    uint32_t magic;
    uint32_t version;
    uint64_t frameId;
    uint64_t monotonicNs;
    uint32_t accepted;
    uint32_t queuedFrameCount;
};

struct SocketChunkHeader {
    uint32_t magic;
    uint32_t type;
    uint32_t size;
};

struct SocketSegmentInfo {
    uint32_t kind;
    uint32_t slotCount;
    uint32_t payloadSize;
    uint32_t slotStride;
    uint64_t byteSize;
};

struct SocketSharedMemoryMessage {
    uint32_t magic;
    uint32_t type;
    uint32_t version;
    uint32_t segmentCount;
    SocketSegmentInfo segments[2];
};

struct SocketPayloadHeader {
    uint32_t magic;
    uint32_t type;
    uint32_t version;
    uint32_t headerBytes;
    uint64_t messageId;
    uint64_t totalBytes;
    uint32_t offset;
    uint32_t payloadBytes;
};
#pragma pack(pop)

struct alignas(64) RingHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t kind;
    uint32_t slotCount;
    uint32_t payloadSize;
    uint32_t slotStride;
    uint32_t reserved0;
    uint32_t reserved1;
    std::atomic<uint64_t> writeSeq;
    std::atomic<uint64_t> readSeq;
    std::atomic<uint64_t> droppedWrites;
};

struct alignas(64) RingSlotHeader {
    std::atomic<uint64_t> beginSeq;
    std::atomic<uint64_t> endSeq;
    uint32_t payloadBytes;
    uint32_t flags;
    uint32_t reserved0;
    uint32_t reserved1;
};

struct TransportConfig {
    const char* trackingName;
    const char* frameName;
    const char* clientFrameName;
    const char* audioOutName;
    const char* audioInName;
    const char* runtimeEventName;
    uint32_t trackingSlots;
    uint32_t frameSlots;
    uint32_t clientFrameSlots;
    uint32_t audioSlots;
    uint32_t eventSlots;
};

inline TransportConfig DefaultConfig() {
    return {
        "/miragebridge_tracking",
        "/miragebridge_frames",
        "/miragebridge_client_frames",
        "/miragebridge_audio_out",
        "/miragebridge_audio_in",
        "/miragebridge_events",
        512,
        8,
        4,
        64,
        128,
    };
}

inline constexpr size_t AlignUp(size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

inline constexpr uint32_t RingSlotStride(uint32_t payloadSize) {
    return static_cast<uint32_t>(AlignUp(sizeof(RingSlotHeader) + payloadSize, 64));
}

inline constexpr size_t RingBytes(uint32_t slotCount, uint32_t payloadSize) {
    return sizeof(RingHeader) + static_cast<size_t>(slotCount) * RingSlotStride(payloadSize);
}

}
