#pragma once

#include <cstdint>

#include "../../../../common/miragebridge_protocol.h"

namespace miragebridge {

class UnixSocketTransport {
public:
    bool Initialize();
    void Shutdown();
    void SendTracking(const XRPacket& packet);
    void SendFrame(const SBSFramePacket& frame);

private:
    bool SendRaw(const void* data, size_t size);
    int sock_ = -1;
    sockaddr_un target_{};
    bool targetReady_ = false;
};

}
