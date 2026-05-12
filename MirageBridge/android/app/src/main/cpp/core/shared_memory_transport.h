#pragma once

#include <cstddef>
#include <cstdint>

#include "miragebridge_protocol.h"

namespace miragebridge {

class SharedMemoryTransport {
public:
    bool Initialize();
    bool IsReady() const;
    void Shutdown();
    bool PushTracking(const XRPacket& packet);
    bool PushFrame(const SBSFramePacket& frame);
    int TrackingFd() const;
    int FrameFd() const;
    size_t TrackingSize() const;
    size_t FrameSize() const;

private:
    struct Segment {
        int fd = -1;
        void* map = nullptr;
        size_t size = 0;
        RingHeader* header = nullptr;
        uint8_t* payload = nullptr;
    };

    bool CreateSegment(const char* name, uint32_t kind, uint32_t slotCount, uint32_t payloadSize, Segment* segment);
    bool PushPacket(const void* data, size_t size, Segment* segment);

    Segment tracking_;
    Segment frames_;
    bool ready_ = false;
};

}
