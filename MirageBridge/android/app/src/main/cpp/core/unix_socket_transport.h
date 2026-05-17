#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

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
    bool PollClientFrame(SBSFramePacket* outFrame);

private:
    bool EnsureConnected();
    bool SendRaw(const void* data, size_t size);
    bool SendPacketChunks(uint32_t type, const void* data, size_t size);
    bool HandleIncomingPacket(const std::vector<uint8_t>& packet, SBSFramePacket* outFrame);
    bool FillAddress(sockaddr_un* addr, socklen_t* len) const;
    std::mutex ioMutex_;
    int sock_ = -1;
    bool connected_ = false;
    uint64_t messageId_ = 1;
    uint64_t incomingMessageId_ = 0;
    uint32_t incomingType_ = 0;
    size_t incomingTotalBytes_ = 0;
    std::vector<uint8_t> incomingBytes_;
};

}
