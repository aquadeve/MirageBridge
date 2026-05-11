#include "core/shared_memory_transport.h"

#include <android/log.h>
#include <fcntl.h>
#include <linux/ashmem.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstring>

#define MB_LOG_TAG "MirageBridgeShm"
#define MB_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, MB_LOG_TAG, __VA_ARGS__)

namespace miragebridge {

namespace {
int CreateAshmem(const char* name, size_t size) {
    int fd = open("/dev/ashmem", O_RDWR);
    if (fd < 0) {
        return -1;
    }
    ioctl(fd, ASHMEM_SET_NAME, name);
    if (ioctl(fd, ASHMEM_SET_SIZE, size) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}
}

bool SharedMemoryTransport::CreateSegment(const char* name, uint32_t slotCount, uint32_t slotSize, Segment* segment) {
    if (!segment) {
        return false;
    }
    const size_t total = sizeof(RingHeader) + static_cast<size_t>(slotCount) * slotSize;
    int fd = CreateAshmem(name, total);
    if (fd < 0) {
        MB_LOGE("ashmem create failed for %s", name);
        return false;
    }

    void* map = mmap(nullptr, total, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        close(fd);
        MB_LOGE("mmap failed for %s", name);
        return false;
    }

    std::memset(map, 0, total);
    segment->fd = fd;
    segment->map = map;
    segment->size = total;
    segment->header = reinterpret_cast<RingHeader*>(map);
    segment->payload = reinterpret_cast<uint8_t*>(map) + sizeof(RingHeader);

    segment->header->magic = kProtocolMagic;
    segment->header->version = kProtocolVersion;
    segment->header->slotCount = slotCount;
    segment->header->slotSize = slotSize;
    segment->header->writeSeq.store(0, std::memory_order_relaxed);
    segment->header->readSeq.store(0, std::memory_order_relaxed);
    return true;
}

bool SharedMemoryTransport::Initialize() {
    const auto cfg = DefaultConfig();
    const bool okTracking = CreateSegment(cfg.trackingName, cfg.trackingSlots, sizeof(XRPacket), &tracking_);
    const bool okFrames = CreateSegment(cfg.frameName, cfg.frameSlots, sizeof(SBSFramePacket), &frames_);
    ready_ = okTracking && okFrames;
    return ready_;
}

bool SharedMemoryTransport::IsReady() const {
    return ready_;
}

bool SharedMemoryTransport::PushPacket(const void* data, size_t size, Segment* segment) {
    if (!segment || !segment->header || !data) {
        return false;
    }
    if (size > segment->header->slotSize) {
        return false;
    }

    const uint64_t writeSeq = segment->header->writeSeq.load(std::memory_order_relaxed);
    const uint32_t slot = static_cast<uint32_t>(writeSeq % segment->header->slotCount);
    uint8_t* dst = segment->payload + static_cast<size_t>(slot) * segment->header->slotSize;
    std::memset(dst, 0, segment->header->slotSize);
    std::memcpy(dst, data, size);
    segment->header->writeSeq.store(writeSeq + 1, std::memory_order_release);
    return true;
}

bool SharedMemoryTransport::PushTracking(const XRPacket& packet) {
    return PushPacket(&packet, sizeof(packet), &tracking_);
}

bool SharedMemoryTransport::PushFrame(const SBSFramePacket& frame) {
    return PushPacket(&frame, sizeof(frame), &frames_);
}

void SharedMemoryTransport::Shutdown() {
    Segment* segments[2] = {&tracking_, &frames_};
    for (Segment* s : segments) {
        if (s->map) {
            munmap(s->map, s->size);
            s->map = nullptr;
        }
        if (s->fd >= 0) {
            close(s->fd);
            s->fd = -1;
        }
        s->header = nullptr;
        s->payload = nullptr;
        s->size = 0;
    }
    ready_ = false;
}

}
