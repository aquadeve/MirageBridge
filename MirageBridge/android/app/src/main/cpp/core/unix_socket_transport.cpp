#include "core/unix_socket_transport.h"

#include <android/log.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>

#define MB_LOG_TAG "MirageBridgeSock"
#define MB_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, MB_LOG_TAG, __VA_ARGS__)

namespace miragebridge {

bool UnixSocketTransport::Initialize() {
    sock_ = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sock_ < 0) {
        return false;
    }

    target_ = {};
    target_.sun_family = AF_UNIX;
    const char* path = "/data/data/com.termux/files/usr/tmp/miragebridge.sock";
    std::snprintf(target_.sun_path, sizeof(target_.sun_path), "%s", path);
    targetReady_ = true;
    return true;
}

void UnixSocketTransport::Shutdown() {
    if (sock_ >= 0) {
        close(sock_);
        sock_ = -1;
    }
}

void UnixSocketTransport::SendTracking(const XRPacket& packet) {
    SocketChunkHeader h{};
    h.magic = kSocketChunkMagic;
    h.type = kSocketChunkTracking;
    h.size = sizeof(XRPacket);
    SendRaw(&h, sizeof(h));
    SendRaw(&packet, sizeof(packet));
}

void UnixSocketTransport::SendFrame(const SBSFramePacket& frame) {
    SocketChunkHeader h{};
    h.magic = kSocketChunkMagic;
    h.type = kSocketChunkFrame;
    h.size = sizeof(SBSFramePacket);
    SendRaw(&h, sizeof(h));

    const uint8_t* data = reinterpret_cast<const uint8_t*>(&frame);
    size_t remaining = sizeof(SBSFramePacket);
    size_t offset = 0;
    while (remaining > 0) {
        const size_t chunk = remaining > 60000 ? 60000 : remaining;
        if (!SendRaw(data + offset, chunk)) {
            return;
        }
        offset += chunk;
        remaining -= chunk;
    }
}

bool UnixSocketTransport::SendRaw(const void* data, size_t size) {
    if (sock_ < 0 || !targetReady_ || !data || size == 0) {
        return false;
    }
    const ssize_t written = sendto(sock_, data, size, 0, reinterpret_cast<sockaddr*>(&target_), sizeof(target_));
    if (written < 0) {
        static int errorCount = 0;
        if (errorCount < 5) {
            MB_LOGE("sendto failed errno=%d", errno);
            ++errorCount;
        }
        return false;
    }
    return true;
}

}
