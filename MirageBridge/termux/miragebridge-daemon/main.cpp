#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include "transport_writer.h"

using namespace miragebridge;

int main() {
    const char* sockPath = "/data/data/com.termux/files/usr/tmp/miragebridge.sock";
    unlink(sockPath);

    int sock = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sock < 0) {
        std::printf("socket create failed\n");
        return 1;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", sockPath);
    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::printf("bind failed: %s\n", sockPath);
        close(sock);
        return 1;
    }

    RingWriter tracking;
    RingWriter frames;
    const auto cfg = DefaultConfig();
    tracking.Create(cfg.trackingName, cfg.trackingSlots, sizeof(XRPacket));
    frames.Create(cfg.frameName, cfg.frameSlots, sizeof(SBSFramePacket));

    std::printf("miragebridge-daemon listening on %s\n", sockPath);

    std::vector<uint8_t> pending;
    SBSFramePacket pendingFrame{};
    bool pendingFrameValid = false;
    uint32_t pendingPayloadBytes = 0;

    for (;;) {
        uint8_t buffer[65536];
        ssize_t n = recv(sock, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        if (pending.empty() && n == static_cast<ssize_t>(sizeof(SocketChunkHeader))) {
            SocketChunkHeader h{};
            std::memcpy(&h, buffer, sizeof(h));
            if (h.magic != kSocketChunkMagic || h.size == 0) {
                continue;
            }
            pending.resize(h.size);
            pendingPayloadBytes = h.type;
            continue;
        }

        if (pending.empty()) {
            continue;
        }

        if (static_cast<size_t>(n) != pending.size()) {
            pending.clear();
            continue;
        }
        std::memcpy(pending.data(), buffer, static_cast<size_t>(n));

        if (pendingPayloadBytes == kSocketChunkTracking && pending.size() == sizeof(XRPacket)) {
            XRPacket packet{};
            std::memcpy(&packet, pending.data(), sizeof(packet));
            if (packet.magic == kProtocolMagic) {
                tracking.Write(&packet, sizeof(packet));
                std::printf("track frame=%llu c=%u\n",
                    static_cast<unsigned long long>(packet.frameId),
                    packet.controllerCount);
            }
        } else if (pendingPayloadBytes == kSocketChunkFrame && pending.size() == sizeof(SBSFrameHeader)) {
            std::memset(&pendingFrame, 0, sizeof(pendingFrame));
            std::memcpy(&pendingFrame.header, pending.data(), sizeof(SBSFrameHeader));
            pendingFrameValid = pendingFrame.header.magic == kProtocolMagic;
        } else if (pendingPayloadBytes == kSocketChunkFrameData &&
                   pending.size() >= sizeof(SocketFrameDataHeader)) {
            SocketFrameDataHeader chunk{};
            std::memcpy(&chunk, pending.data(), sizeof(chunk));
            const uint8_t* chunkPayload = pending.data() + sizeof(chunk);
            const uint32_t chunkBytes = chunk.payloadSize;
            const bool chunkSizeMatches = pending.size() == sizeof(chunk) + chunkBytes;
            const bool frameIdMatches = pendingFrameValid && chunk.frameId == pendingFrame.header.frameId;
            const bool chunkInRange = chunk.payloadOffset <= sizeof(pendingFrame.payload) &&
                chunkBytes <= sizeof(pendingFrame.payload) - chunk.payloadOffset;
            if (chunkSizeMatches && frameIdMatches && chunkInRange) {
                std::memcpy(pendingFrame.payload + chunk.payloadOffset, chunkPayload, chunkBytes);
                const uint32_t received = chunk.payloadOffset + chunkBytes;
                if (received >= pendingFrame.header.payloadBytes) {
                    frames.Write(&pendingFrame, sizeof(pendingFrame));
                    std::printf("frame id=%llu payload=%u\n",
                        static_cast<unsigned long long>(pendingFrame.header.frameId),
                        pendingFrame.header.payloadBytes);
                    pendingFrameValid = false;
                }
            }
        }

        pending.clear();
    }

    close(sock);
    return 0;
}
