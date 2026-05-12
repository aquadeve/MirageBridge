#include "core/shared_memory_transport.h"

#include <android/sharedmem.h>
#include <android/log.h>
#include <fcntl.h>
#include <linux/ashmem.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstring>

#include "ring_buffer.h"

#define MB_LOG_TAG "MirageBridgeShm"
#define MB_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, MB_LOG_TAG, __VA_ARGS__)

namespace miragebridge {

namespace {
int CreateAshmem(const char* name, size_t size) {
#if __ANDROID_API__ >= 26
    int sharedFd = ASharedMemory_create(name, size);
    if (sharedFd >= 0) {
        ASharedMemory_setProt(sharedFd, PROT_READ | PROT_WRITE);
        return sharedFd;
    }
#endif
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

bool SharedMemoryTransport::CreateSegment(const char* name, uint32_t kind, uint32_t slotCount, uint32_t payloadSize, Segment* segment) {
    if (!segment) {
        return false;
    }
    const size_t total = RingBytes(slotCount, payloadSize);
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

    InitializeRing(map, kind, slotCount, payloadSize);
    return true;
}

bool SharedMemoryTransport::Initialize() {
    const auto cfg = DefaultConfig();
    const bool okTracking = CreateSegment("miragebridge_tracking", kRingKindTracking, cfg.trackingSlots, sizeof(XRPacket), &tracking_);
    const bool okFrames = CreateSegment("miragebridge_frames", kRingKindFrame, cfg.frameSlots, sizeof(SBSFramePacket), &frames_);
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
    return RingWrite(segment->header, segment->payload, data, size);
}

bool SharedMemoryTransport::PushTracking(const XRPacket& packet) {
    return PushPacket(&packet, sizeof(packet), &tracking_);
}

bool SharedMemoryTransport::PushFrame(const SBSFramePacket& frame) {
    return PushPacket(&frame, sizeof(frame), &frames_);
}

int SharedMemoryTransport::TrackingFd() const {
    return tracking_.fd;
}

int SharedMemoryTransport::FrameFd() const {
    return frames_.fd;
}

size_t SharedMemoryTransport::TrackingSize() const {
    return tracking_.size;
}

size_t SharedMemoryTransport::FrameSize() const {
    return frames_.size;
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
