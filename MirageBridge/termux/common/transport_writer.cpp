#include "transport_writer.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstring>

namespace miragebridge {

bool RingWriter::Create(const char* name, uint32_t slotCount, uint32_t slotSize) {
    fd_ = shm_open(name, O_CREAT | O_RDWR, 0666);
    if (fd_ < 0) {
        return false;
    }

    size_ = sizeof(RingHeader) + static_cast<size_t>(slotCount) * slotSize;
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

    std::memset(map_, 0, size_);
    header_ = reinterpret_cast<RingHeader*>(map_);
    payload_ = reinterpret_cast<uint8_t*>(map_) + sizeof(RingHeader);
    header_->magic = kProtocolMagic;
    header_->version = kProtocolVersion;
    header_->slotCount = slotCount;
    header_->slotSize = slotSize;
    header_->writeSeq.store(0, std::memory_order_relaxed);
    header_->readSeq.store(0, std::memory_order_relaxed);
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
    if (!header_ || !payload_ || !data || size > header_->slotSize) {
        return false;
    }

    const uint64_t writeSeq = header_->writeSeq.load(std::memory_order_relaxed);
    const uint32_t slot = static_cast<uint32_t>(writeSeq % header_->slotCount);
    uint8_t* dst = payload_ + static_cast<size_t>(slot) * header_->slotSize;
    std::memset(dst, 0, header_->slotSize);
    std::memcpy(dst, data, size);
    header_->writeSeq.store(writeSeq + 1, std::memory_order_release);
    return true;
}

}
