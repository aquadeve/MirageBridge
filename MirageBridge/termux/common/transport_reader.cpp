#include "transport_reader.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>

#include "ring_buffer.h"

namespace miragebridge {

bool RingReader::Open(const char* name, size_t expectedSlotSize) {
    fd_ = shm_open(name, O_RDWR, 0);
    if (fd_ < 0) {
        return false;
    }

    struct stat st {};
    if (fstat(fd_, &st) != 0 || st.st_size <= 0) {
        Close();
        return false;
    }
    size_ = static_cast<size_t>(st.st_size);

    map_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
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
    if (expectedSlotSize > header_->payloadSize) {
        Close();
        return false;
    }
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
    return RingReadLatest(header_, payload_, outData, outSize, outSeq);
}

bool RingReader::ReadSequence(uint64_t sequence, void* outData, size_t outSize) {
    return RingReadSequence(header_, payload_, sequence, outData, outSize);
}

uint64_t RingReader::WriteSequence() const {
    if (!header_) {
        return 0;
    }
    return header_->writeSeq.load(std::memory_order_acquire);
}

}
