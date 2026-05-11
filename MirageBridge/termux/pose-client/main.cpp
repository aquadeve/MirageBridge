#include <chrono>
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
            std::printf("frame=%llu predNs=%llu quat=(%f,%f,%f,%f)\n",
                static_cast<unsigned long long>(packet.frameId),
                static_cast<unsigned long long>(packet.predictedDisplayNs),
                packet.rot[0], packet.rot[1], packet.rot[2], packet.rot[3]);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }

    return 0;
}
