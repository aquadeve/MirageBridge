#pragma once

#include <cstddef>
#include <cstdint>

#include "../../common/miragebridge_protocol.h"

namespace miragebridge {

class RingReader {
public:
    bool Open(const char* name, size_t expectedSlotSize);
    void Close();
    bool ReadLatest(void* outData, size_t outSize, uint64_t* outSeq);
    bool ReadSequence(uint64_t sequence, void* outData, size_t outSize);
    uint64_t WriteSequence() const;

private:
    int fd_ = -1;
    void* map_ = nullptr;
    size_t size_ = 0;
    RingHeader* header_ = nullptr;
    uint8_t* payload_ = nullptr;
};

}
