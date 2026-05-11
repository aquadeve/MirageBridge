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
    uint32_t pendingType = 0;
    uint32_t pendingSize = 0;

    for (;;) {
        uint8_t buffer[65536];
        ssize_t n = recv(sock, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        if (pending.empty()) {
            if (n == static_cast<ssize_t>(sizeof(SocketChunkHeader))) {
                SocketChunkHeader h{};
                std::memcpy(&h, buffer, sizeof(h));
                if (h.magic != kSocketChunkMagic || h.size == 0) {
                    continue;
                }
                pendingType = h.type;
                pendingSize = h.size;
                pending.reserve(h.size);
                continue;
            }
            continue;
        }

        pending.insert(pending.end(), buffer, buffer + n);
        if (pending.size() < pendingSize) {
            continue;
        }

        if (pendingType == kSocketChunkTracking && pendingSize == sizeof(XRPacket)) {
            XRPacket packet{};
            std::memcpy(&packet, pending.data(), sizeof(packet));
            if (packet.magic == kProtocolMagic) {
                tracking.Write(&packet, sizeof(packet));
                std::printf("track frame=%llu c=%u\n",
                    static_cast<unsigned long long>(packet.frameId),
                    packet.controllerCount);
            }
        } else if (pendingType == kSocketChunkFrame && pendingSize == sizeof(SBSFramePacket)) {
            SBSFramePacket frame{};
            std::memcpy(&frame, pending.data(), sizeof(frame));
            if (frame.header.magic == kProtocolMagic) {
                frames.Write(&frame, sizeof(frame));
                std::printf("frame id=%llu payload=%u\n",
                    static_cast<unsigned long long>(frame.header.frameId),
                    frame.header.payloadBytes);
            }
        }

        pending.clear();
        pendingType = 0;
        pendingSize = 0;
    }

    close(sock);
    return 0;
}
