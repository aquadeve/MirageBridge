#include "core/unix_socket_transport.h"

#include <android/log.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <vector>

#define MB_LOG_TAG "MirageBridgeSock"
#define MB_LOGI(...) __android_log_print(ANDROID_LOG_INFO, MB_LOG_TAG, __VA_ARGS__)
#define MB_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, MB_LOG_TAG, __VA_ARGS__)

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace miragebridge {

bool UnixSocketTransport::FillAddress(sockaddr_un* addr, socklen_t* len) const {
    if (!addr || !len) {
        return false;
    }
    *addr = {};
    addr->sun_family = AF_UNIX;
    addr->sun_path[0] = '\0';
    const size_t nameLen = std::strlen(kAndroidControlSocketName);
    if (nameLen + 1 >= sizeof(addr->sun_path)) {
        return false;
    }
    std::memcpy(addr->sun_path + 1, kAndroidControlSocketName, nameLen);
    *len = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + 1 + nameLen);
    return true;
}

bool UnixSocketTransport::Initialize() {
    return EnsureConnected();
}

bool UnixSocketTransport::EnsureConnected() {
    if (connected_ && sock_ >= 0) {
        return true;
    }

    if (sock_ >= 0) {
        close(sock_);
        sock_ = -1;
    }

    sock_ = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (sock_ < 0) {
        sock_ = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    }
    if (sock_ < 0) {
        static int errorCount = 0;
        if (errorCount++ < 5) {
            MB_LOGE("socket create failed errno=%d", errno);
        }
        return false;
    }

    sockaddr_un addr{};
    socklen_t len = 0;
    if (!FillAddress(&addr, &len) || connect(sock_, reinterpret_cast<sockaddr*>(&addr), len) != 0) {
        static int errorCount = 0;
        if (errorCount++ < 5) {
            MB_LOGE("connect abstract socket failed errno=%d", errno);
        }
        close(sock_);
        sock_ = -1;
        connected_ = false;
        return false;
    }

    connected_ = true;
    MB_LOGI("connected to Termux abstract socket");
    return true;
}

bool UnixSocketTransport::IsReady() const {
    return connected_ && sock_ >= 0;
}

void UnixSocketTransport::Shutdown() {
    if (sock_ >= 0) {
        close(sock_);
        sock_ = -1;
    }
    connected_ = false;
}

bool UnixSocketTransport::SendSharedMemoryHandles(int trackingFd,
                                                  size_t trackingSize,
                                                  int frameFd,
                                                  size_t frameSize,
                                                  uint32_t trackingSlots,
                                                  uint32_t frameSlots) {
    if (trackingFd < 0 || frameFd < 0 || !EnsureConnected()) {
        return false;
    }

    SocketSharedMemoryMessage msg{};
    msg.magic = kSocketChunkMagic;
    msg.type = kSocketChunkSharedMemory;
    msg.version = kProtocolVersion;
    msg.segmentCount = 2;
    msg.segments[0] = {kRingKindTracking, trackingSlots, sizeof(XRPacket), RingSlotStride(sizeof(XRPacket)), trackingSize};
    msg.segments[1] = {kRingKindFrame, frameSlots, sizeof(SBSFramePacket), RingSlotStride(sizeof(SBSFramePacket)), frameSize};

    int fds[2] = {trackingFd, frameFd};
    char control[CMSG_SPACE(sizeof(fds))];
    std::memset(control, 0, sizeof(control));

    iovec iov{};
    iov.iov_base = &msg;
    iov.iov_len = sizeof(msg);

    msghdr mh{};
    mh.msg_iov = &iov;
    mh.msg_iovlen = 1;
    mh.msg_control = control;
    mh.msg_controllen = sizeof(control);

    cmsghdr* cmsg = CMSG_FIRSTHDR(&mh);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(fds));
    std::memcpy(CMSG_DATA(cmsg), fds, sizeof(fds));
    mh.msg_controllen = CMSG_SPACE(sizeof(fds));

    const ssize_t written = sendmsg(sock_, &mh, MSG_NOSIGNAL);
    if (written != static_cast<ssize_t>(sizeof(msg))) {
        MB_LOGE("sendmsg fd handoff failed errno=%d", errno);
        Shutdown();
        return false;
    }
    return true;
}

void UnixSocketTransport::SendTracking(const XRPacket& packet) {
    SendPacketChunks(kSocketChunkTracking, &packet, sizeof(packet));
}

void UnixSocketTransport::SendFrame(const SBSFramePacket& frame) {
    SendPacketChunks(kSocketChunkFrame, &frame, sizeof(frame));
}

bool UnixSocketTransport::SendPacketChunks(uint32_t type, const void* data, size_t size) {
    if (!data || size == 0 || !EnsureConnected()) {
        return false;
    }

    const auto* bytes = reinterpret_cast<const uint8_t*>(data);
    const uint64_t id = messageId_++;
    size_t offset = 0;
    while (offset < size) {
        const uint32_t chunk = static_cast<uint32_t>(std::min<size_t>(kSocketMaxPayload, size - offset));
        SocketPayloadHeader header{};
        header.magic = kSocketChunkMagic;
        header.type = type;
        header.version = kProtocolVersion;
        header.headerBytes = sizeof(SocketPayloadHeader);
        header.messageId = id;
        header.totalBytes = size;
        header.offset = static_cast<uint32_t>(offset);
        header.payloadBytes = chunk;

        std::vector<uint8_t> packet(sizeof(header) + chunk);
        std::memcpy(packet.data(), &header, sizeof(header));
        std::memcpy(packet.data() + sizeof(header), bytes + offset, chunk);
        if (!SendRaw(packet.data(), packet.size())) {
            return false;
        }
        offset += chunk;
    }
    return true;
}

bool UnixSocketTransport::SendRaw(const void* data, size_t size) {
    if (sock_ < 0 || !connected_ || !data || size == 0) {
        return false;
    }
    const ssize_t written = send(sock_, data, size, MSG_NOSIGNAL);
    if (written != static_cast<ssize_t>(size)) {
        static int errorCount = 0;
        if (errorCount++ < 5) {
            MB_LOGE("send failed errno=%d", errno);
        }
        Shutdown();
        return false;
    }
    return true;
}

}
