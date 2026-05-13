#include <cstdint>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

#include "mirage_runtime.hpp"

int main() {
    mbr_runtime_config cfg{};
    cfg.application_name = "mbr-submit-sbs";

    miragebridge::sdk::Runtime runtime(cfg);
    runtime.connect("local");

    constexpr uint32_t width = 2048;
    constexpr uint32_t height = 1024;
    constexpr uint32_t stride = width * 4;
    std::vector<uint8_t> pixels(static_cast<size_t>(stride) * height);

    uint64_t frameId = 0;
    for (;;) {
        const auto timing = runtime.waitFrame(1000000000ull);
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                const bool left = x < width / 2;
                const size_t offset = static_cast<size_t>(y) * stride + static_cast<size_t>(x) * 4;
                pixels[offset + 0] = left ? static_cast<uint8_t>(frameId) : 32;
                pixels[offset + 1] = static_cast<uint8_t>((x + y + frameId) & 0xff);
                pixels[offset + 2] = left ? 32 : static_cast<uint8_t>(frameId);
                pixels[offset + 3] = 255;
            }
        }

        mbr_sbs_frame_desc frame{};
        frame.frame_id = frameId++;
        frame.target_display_ns = timing.predicted_display_ns;
        frame.width = width;
        frame.height = height;
        frame.stride_bytes = stride;
        frame.pixel_format = MBR_PIXEL_FORMAT_RGBA8;
        frame.pixels = pixels.data();
        frame.bytes = static_cast<uint32_t>(pixels.size());
        runtime.submitSbsFrame(frame);

        const auto metrics = runtime.metrics();
        std::printf("submitted=%llu posePackets=%llu\n",
                    static_cast<unsigned long long>(metrics.frames_submitted),
                    static_cast<unsigned long long>(metrics.pose_packets_read));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
