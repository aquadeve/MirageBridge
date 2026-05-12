#include "transport_writer.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstring>

#include "ring_buffer.h"

namespace miragebridge {

bool RingWriter::Create(const char* name, uint32_t slotCount, uint32_t slotSize) {
    fd_ = shm_open(name, O_CREAT | O_RDWR, 0666);
    if (fd_ < 0) {
        return false;
    }

    size_ = RingBytes(slotCount, slotSize);
    if (ftruncate(fd_, static_cast<off_t>(size_)) != 0) {
        Close();
        return false;
    }

    map_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (map_ == MAP_FAILED) {
        map_ = nullptr;
        Close();
        return false;
    }

    header_ = reinterpret_cast<RingHeader*>(map_);
    payload_ = reinterpret_cast<uint8_t*>(map_) + sizeof(RingHeader);
    const uint32_t kind = slotSize == sizeof(XRPacket) ? kRingKindTracking : kRingKindFrame;
    InitializeRing(map_, kind, slotCount, slotSize);
    return true;
}

void RingWriter::Close() {
    if (map_) {
        munmap(map_, size_);
        map_ = nullptr;
    }
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
    size_ = 0;
    header_ = nullptr;
    payload_ = nullptr;
}

bool RingWriter::Write(const void* data, size_t size) {
    return RingWrite(header_, payload_, data, size);
}

}
