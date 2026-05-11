#include "transport_reader.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstring>

namespace miragebridge {

bool RingReader::Open(const char* name, size_t expectedSlotSize) {
    fd_ = shm_open(name, O_RDONLY, 0);
    if (fd_ < 0) {
        return false;
    }

    const size_t maxMap = sizeof(RingHeader) + expectedSlotSize * 1024;
    map_ = mmap(nullptr, maxMap, PROT_READ, MAP_SHARED, fd_, 0);
    if (map_ == MAP_FAILED) {
        close(fd_);
        fd_ = -1;
        map_ = nullptr;
        return false;
    }

    header_ = reinterpret_cast<RingHeader*>(map_);
    if (header_->magic != kProtocolMagic) {
        Close();
        return false;
    }

    size_ = sizeof(RingHeader) + static_cast<size_t>(header_->slotCount) * header_->slotSize;
    payload_ = reinterpret_cast<uint8_t*>(map_) + sizeof(RingHeader);
    return true;
}

void RingReader::Close() {
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

bool RingReader::ReadLatest(void* outData, size_t outSize, uint64_t* outSeq) {
    if (!header_ || !payload_ || !outData || outSize > header_->slotSize) {
        return false;
    }

    uint64_t writeSeq = header_->writeSeq.load(std::memory_order_acquire);
    if (writeSeq == 0) {
        return false;
    }

    uint64_t seq = writeSeq - 1;
    uint32_t slot = static_cast<uint32_t>(seq % header_->slotCount);
    uint8_t* src = payload_ + static_cast<size_t>(slot) * header_->slotSize;
    std::memcpy(outData, src, outSize);
    if (outSeq) {
        *outSeq = seq;
    }
    return true;
}

}
