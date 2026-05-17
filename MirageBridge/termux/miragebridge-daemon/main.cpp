#include <poll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include "ring_buffer.h"
#include "transport_reader.h"
#include "transport_writer.h"

using namespace miragebridge;

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace {

struct RemoteRing {
    int fd = -1;
    void* map = nullptr;
    size_t size = 0;
    RingHeader* header = nullptr;
    uint8_t* payload = nullptr;
    uint64_t nextSeq = 0;
};

struct ChunkAccumulator {
    uint64_t messageId = 0;
    uint32_t type = 0;
    size_t totalBytes = 0;
    std::vector<uint8_t> bytes;

    void Reset() {
        messageId = 0;
        type = 0;
        totalBytes = 0;
        bytes.clear();
    }
};

bool FillAbstractAddress(sockaddr_un* addr, socklen_t* len) {
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

int CreateListener() {
    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    }
    if (fd < 0) {
        std::printf("socket create failed: %s\n", std::strerror(errno));
        return -1;
    }

    sockaddr_un addr{};
    socklen_t len = 0;
    if (!FillAbstractAddress(&addr, &len)) {
        close(fd);
        return -1;
    }
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), len) != 0) {
        std::printf("bind abstract socket failed: %s\n", std::strerror(errno));
        close(fd);
        return -1;
    }
    if (listen(fd, 1) != 0) {
        std::printf("listen failed: %s\n", std::strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

void CloseRemote(RemoteRing* ring) {
    if (!ring) {
        return;
    }
    if (ring->map) {
        munmap(ring->map, ring->size);
    }
    if (ring->fd >= 0) {
        close(ring->fd);
    }
    *ring = {};
    ring->fd = -1;
}

bool MapRemote(int fd, const SocketSegmentInfo& info, RemoteRing* ring) {
    if (!ring || fd < 0 || info.byteSize < sizeof(RingHeader)) {
        return false;
    }
    CloseRemote(ring);
    void* map = mmap(nullptr, static_cast<size_t>(info.byteSize), PROT_READ, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        std::printf("mmap remote ring failed kind=%u: %s\n", info.kind, std::strerror(errno));
        close(fd);
        return false;
    }

    auto* header = reinterpret_cast<RingHeader*>(map);
    if (header->magic != kProtocolMagic || header->version != kProtocolVersion || header->kind != info.kind) {
        std::printf("remote ring header mismatch kind=%u magic=0x%x version=%u\n", info.kind, header->magic, header->version);
        munmap(map, static_cast<size_t>(info.byteSize));
        close(fd);
        return false;
    }

    ring->fd = fd;
    ring->map = map;
    ring->size = static_cast<size_t>(info.byteSize);
    ring->header = header;
    ring->payload = reinterpret_cast<uint8_t*>(map) + sizeof(RingHeader);
    ring->nextSeq = header->writeSeq.load(std::memory_order_acquire);
    return true;
}

void MirrorTracking(RemoteRing* remote, RingWriter* local) {
    if (!remote || !remote->header || !local) {
        return;
    }
    const uint64_t writeSeq = remote->header->writeSeq.load(std::memory_order_acquire);
    if (remote->nextSeq + remote->header->slotCount < writeSeq) {
        remote->nextSeq = writeSeq - remote->header->slotCount;
    }
    XRPacket packet{};
    while (remote->nextSeq < writeSeq) {
        if (RingReadSequence(remote->header, remote->payload, remote->nextSeq, &packet, sizeof(packet))) {
            local->Write(&packet, sizeof(packet));
        }
        ++remote->nextSeq;
    }
}

void MirrorFrame(RemoteRing* remote, RingWriter* local, SBSFramePacket* scratch) {
    if (!remote || !remote->header || !local || !scratch) {
        return;
    }
    const uint64_t writeSeq = remote->header->writeSeq.load(std::memory_order_acquire);
    if (writeSeq == 0 || remote->nextSeq == writeSeq) {
        return;
    }
    const uint64_t latest = writeSeq - 1;
    if (RingReadSequence(remote->header, remote->payload, latest, scratch, sizeof(*scratch))) {
        local->Write(scratch, sizeof(*scratch));
        remote->nextSeq = writeSeq;
    }
}

void ExtractFds(msghdr* mh, std::vector<int>* fds) {
    for (cmsghdr* cmsg = CMSG_FIRSTHDR(mh); cmsg; cmsg = CMSG_NXTHDR(mh, cmsg)) {
        if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) {
            continue;
        }
        const size_t bytes = cmsg->cmsg_len - CMSG_LEN(0);
        const size_t count = bytes / sizeof(int);
        const int* fdData = reinterpret_cast<const int*>(CMSG_DATA(cmsg));
        for (size_t i = 0; i < count; ++i) {
            fds->push_back(fdData[i]);
        }
    }
}

bool ReceivePacket(int fd, std::vector<uint8_t>* bytes, std::vector<int>* fds) {
    bytes->assign(kSocketMaxPayload + sizeof(SocketPayloadHeader) + 256, 0);
    char control[CMSG_SPACE(sizeof(int) * 4)];
    std::memset(control, 0, sizeof(control));

    iovec iov{};
    iov.iov_base = bytes->data();
    iov.iov_len = bytes->size();

    msghdr mh{};
    mh.msg_iov = &iov;
    mh.msg_iovlen = 1;
    mh.msg_control = control;
    mh.msg_controllen = sizeof(control);

    const ssize_t n = recvmsg(fd, &mh, 0);
    if (n <= 0) {
        return false;
    }
    bytes->resize(static_cast<size_t>(n));
    ExtractFds(&mh, fds);
    return true;
}

bool SendRaw(int fd, const void* data, size_t size) {
    if (fd < 0 || !data || size == 0) {
        return false;
    }
    const ssize_t written = send(fd, data, size, MSG_NOSIGNAL);
    return written == static_cast<ssize_t>(size);
}

bool SendPacketChunks(int fd, uint32_t type, const void* data, size_t size, uint64_t* nextMessageId) {
    if (fd < 0 || !data || size == 0 || !nextMessageId) {
        return false;
    }
    const auto* bytes = reinterpret_cast<const uint8_t*>(data);
    const uint64_t id = (*nextMessageId)++;
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
        if (!SendRaw(fd, packet.data(), packet.size())) {
            return false;
        }
        offset += chunk;
    }
    return true;
}

bool MirrorClientFrameToAndroid(int client,
                                RingReader* clientFrames,
                                bool* clientFramesOpen,
                                uint64_t* lastClientFrameSeq,
                                SBSFramePacket* scratch,
                                uint64_t* nextMessageId) {
    if (!clientFrames || !clientFramesOpen || !lastClientFrameSeq || !scratch || !nextMessageId) {
        return true;
    }

    if (!*clientFramesOpen) {
        const auto cfg = DefaultConfig();
        *clientFramesOpen = clientFrames->Open(cfg.clientFrameName, sizeof(SBSFramePacket));
        if (!*clientFramesOpen) {
            return true;
        }
        *lastClientFrameSeq = UINT64_MAX;
        std::printf("opened client frame ring %s\n", cfg.clientFrameName);
    }

    uint64_t seq = 0;
    if (!clientFrames->ReadLatest(scratch, sizeof(*scratch), &seq)) {
        return true;
    }
    if (seq == *lastClientFrameSeq) {
        return true;
    }
    *lastClientFrameSeq = seq;
    if (scratch->header.magic != kProtocolMagic || scratch->header.version != kProtocolVersion) {
        return true;
    }

    return SendPacketChunks(client, kSocketChunkClientFrame, scratch, sizeof(*scratch), nextMessageId);
}

void HandlePayloadChunk(const std::vector<uint8_t>& packet,
                        ChunkAccumulator* accumulator,
                        RingWriter* tracking,
                        RingWriter* frames,
                        SBSFramePacket* frameScratch) {
    if (packet.size() < sizeof(SocketPayloadHeader)) {
        return;
    }
    SocketPayloadHeader header{};
    std::memcpy(&header, packet.data(), sizeof(header));
    if (header.magic != kSocketChunkMagic || header.headerBytes != sizeof(SocketPayloadHeader)) {
        return;
    }
    if (packet.size() < sizeof(SocketPayloadHeader) + header.payloadBytes) {
        return;
    }
    if (accumulator->messageId != header.messageId || accumulator->type != header.type) {
        accumulator->Reset();
        accumulator->messageId = header.messageId;
        accumulator->type = header.type;
        accumulator->totalBytes = static_cast<size_t>(header.totalBytes);
        accumulator->bytes.assign(accumulator->totalBytes, 0);
    }
    if (static_cast<size_t>(header.offset) + header.payloadBytes > accumulator->bytes.size()) {
        accumulator->Reset();
        return;
    }

    std::memcpy(accumulator->bytes.data() + header.offset,
                packet.data() + sizeof(SocketPayloadHeader),
                header.payloadBytes);
    if (static_cast<size_t>(header.offset) + header.payloadBytes < accumulator->totalBytes) {
        return;
    }

    if (header.type == kSocketChunkTracking && accumulator->bytes.size() == sizeof(XRPacket)) {
        XRPacket xr{};
        std::memcpy(&xr, accumulator->bytes.data(), sizeof(xr));
        if (xr.magic == kProtocolMagic) {
            tracking->Write(&xr, sizeof(xr));
        }
    } else if (header.type == kSocketChunkFrame && accumulator->bytes.size() == sizeof(SBSFramePacket)) {
        std::memcpy(frameScratch, accumulator->bytes.data(), sizeof(SBSFramePacket));
        if (frameScratch->header.magic == kProtocolMagic) {
            frames->Write(frameScratch, sizeof(*frameScratch));
        }
    }
    accumulator->Reset();
}

}

int main() {
    const auto cfg = DefaultConfig();
    RingWriter tracking;
    RingWriter frames;
    if (!tracking.Create(cfg.trackingName, cfg.trackingSlots, sizeof(XRPacket))) {
        std::printf("unable to create tracking POSIX shm\n");
        return 1;
    }
    if (!frames.Create(cfg.frameName, cfg.frameSlots, sizeof(SBSFramePacket))) {
        std::printf("unable to create frame POSIX shm\n");
        return 1;
    }

    int listener = CreateListener();
    if (listener < 0) {
        return 1;
    }

    std::printf("miragebridge-daemon listening on abstract socket @%s\n", kAndroidControlSocketName);
    auto frameScratch = std::make_unique<SBSFramePacket>();

    for (;;) {
        int client = accept(listener, nullptr, nullptr);
        if (client < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        std::printf("android bridge connected\n");

        RemoteRing remoteTracking;
        RemoteRing remoteFrames;
        RingReader clientFrames;
        bool clientFramesOpen = false;
        uint64_t lastClientFrameSeq = UINT64_MAX;
        uint64_t nextOutgoingMessageId = 1;
        ChunkAccumulator accumulator;

        bool connected = true;
        while (connected) {
            MirrorTracking(&remoteTracking, &tracking);
            MirrorFrame(&remoteFrames, &frames, frameScratch.get());
            if (!MirrorClientFrameToAndroid(client,
                                            &clientFrames,
                                            &clientFramesOpen,
                                            &lastClientFrameSeq,
                                            frameScratch.get(),
                                            &nextOutgoingMessageId)) {
                connected = false;
                break;
            }

            pollfd pfd{};
            pfd.fd = client;
            pfd.events = POLLIN;
            const int pollResult = poll(&pfd, 1, 5);
            if (pollResult < 0) {
                connected = false;
                break;
            }
            if (pollResult == 0) {
                continue;
            }
            if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                connected = false;
                break;
            }

            std::vector<uint8_t> packet;
            std::vector<int> fds;
            if (!ReceivePacket(client, &packet, &fds)) {
                connected = false;
                break;
            }

            if (packet.size() == sizeof(SocketSharedMemoryMessage)) {
                SocketSharedMemoryMessage msg{};
                std::memcpy(&msg, packet.data(), sizeof(msg));
                if (msg.magic == kSocketChunkMagic &&
                    msg.type == kSocketChunkSharedMemory &&
                    msg.segmentCount == 2 &&
                    fds.size() >= 2) {
                    MapRemote(fds[0], msg.segments[0], &remoteTracking);
                    MapRemote(fds[1], msg.segments[1], &remoteFrames);
                    for (size_t i = 2; i < fds.size(); ++i) {
                        close(fds[i]);
                    }
                    std::printf("mapped Android shared rings: tracking=%zu bytes frames=%zu bytes\n",
                                remoteTracking.size,
                                remoteFrames.size);
                    continue;
                }
            }

            for (int extraFd : fds) {
                close(extraFd);
            }
            HandlePayloadChunk(packet, &accumulator, &tracking, &frames, frameScratch.get());
        }

        std::printf("android bridge disconnected\n");
        clientFrames.Close();
        CloseRemote(&remoteTracking);
        CloseRemote(&remoteFrames);
        close(client);
    }

    close(listener);
    return 0;
}
