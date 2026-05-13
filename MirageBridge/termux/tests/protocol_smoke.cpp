#include <cstdio>
#include <cstring>

#include "miragebridge_protocol.h"
#include "transport_reader.h"
#include "transport_writer.h"

int main() {
    constexpr const char* kName = "/miragebridge_protocol_smoke";
    miragebridge::RingWriter writer;
    if (!writer.Create(kName, 4, sizeof(miragebridge::XRPacket))) {
        std::printf("writer create failed\n");
        return 1;
    }

    miragebridge::XRPacket in{};
    in.magic = miragebridge::kProtocolMagic;
    in.version = miragebridge::kProtocolVersion;
    in.frameId = 42;
    in.rot[3] = 1.0f;
    in.displayHz = 72;
    if (!writer.Write(&in, sizeof(in))) {
        std::printf("writer write failed\n");
        return 1;
    }

    miragebridge::RingReader reader;
    if (!reader.Open(kName, sizeof(miragebridge::XRPacket))) {
        std::printf("reader open failed\n");
        return 1;
    }

    miragebridge::XRPacket out{};
    uint64_t seq = 0;
    if (!reader.ReadLatest(&out, sizeof(out), &seq)) {
        std::printf("reader read failed\n");
        return 1;
    }
    if (out.magic != miragebridge::kProtocolMagic || out.frameId != 42 || out.displayHz != 72) {
        std::printf("packet mismatch\n");
        return 1;
    }
    return 0;
}
