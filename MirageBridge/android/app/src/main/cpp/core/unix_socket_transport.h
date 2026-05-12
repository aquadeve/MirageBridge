#pragma once

#include <cstdint>

#include <cstddef>
#include <sys/un.h>

#include "miragebridge_protocol.h"

namespace miragebridge {

class UnixSocketTransport {
public:
    bool Initialize();
    void Shutdown();
    bool IsReady() const;
    bool SendSharedMemoryHandles(int trackingFd,
                                 size_t trackingSize,
                                 int frameFd,
                                 size_t frameSize,
                                 uint32_t trackingSlots,
                                 uint32_t frameSlots);
    void SendTracking(const XRPacket& packet);
    void SendFrame(const SBSFramePacket& frame);

private:
    bool EnsureConnected();
    bool SendRaw(const void* data, size_t size);
    bool SendPacketChunks(uint32_t type, const void* data, size_t size);
    bool FillAddress(sockaddr_un* addr, socklen_t* len) const;
    int sock_ = -1;
    bool connected_ = false;
    uint64_t messageId_ = 1;
};

}
