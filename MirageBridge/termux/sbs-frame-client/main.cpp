#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

#include "transport_reader.h"

using namespace miragebridge;

int main() {
    RingReader reader;
    const auto cfg = DefaultConfig();
    if (!reader.Open(cfg.frameName, sizeof(SBSFramePacket))) {
        std::printf("unable to open frame ring\n");
        return 1;
    }

    uint64_t seq = 0;
    auto frame = std::make_unique<SBSFramePacket>();
    for (;;) {
        if (reader.ReadLatest(frame.get(), sizeof(*frame), &seq)) {
            const auto& header = frame->header;
            std::printf("frame=%llu target=%llu dims=%ux%u stride=%u payload=%u px0=%u,%u,%u,%u\n",
                static_cast<unsigned long long>(header.frameId),
                static_cast<unsigned long long>(header.targetDisplayNs),
                header.sbsWidth,
                header.sbsHeight,
                header.strideBytes,
                header.payloadBytes,
                frame->payload[0],
                frame->payload[1],
                frame->payload[2],
                frame->payload[3]);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    return 0;
}
