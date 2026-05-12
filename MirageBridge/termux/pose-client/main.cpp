#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

#include "transport_reader.h"

using namespace miragebridge;

int main() {
    RingReader reader;
    const auto cfg = DefaultConfig();
    if (!reader.Open(cfg.trackingName, sizeof(XRPacket))) {
        std::printf("unable to open tracking ring\n");
        return 1;
    }

    uint64_t seq = 0;
    for (;;) {
        XRPacket packet{};
        if (reader.ReadLatest(&packet, sizeof(packet), &seq)) {
            const float yaw = std::atan2(2.0f * (packet.rot[3] * packet.rot[1] + packet.rot[0] * packet.rot[2]),
                                         1.0f - 2.0f * (packet.rot[1] * packet.rot[1] + packet.rot[2] * packet.rot[2]));
            std::printf("frame=%llu predNs=%llu pos=(%+.3f,%+.3f,%+.3f) yaw=%+.2f quat=(%+.3f,%+.3f,%+.3f,%+.3f) vel=(%+.3f,%+.3f,%+.3f)\n",
                static_cast<unsigned long long>(packet.frameId),
                static_cast<unsigned long long>(packet.predictedDisplayNs),
                packet.pos[0], packet.pos[1], packet.pos[2],
                yaw,
                packet.rot[0], packet.rot[1], packet.rot[2], packet.rot[3],
                packet.linearVel[0], packet.linearVel[1], packet.linearVel[2]);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }

    return 0;
}
