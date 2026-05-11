#pragma once

#include <cstddef>
#include <cstdint>

#include "../../common/miragebridge_protocol.h"

namespace miragebridge {

class RingWriter {
public:
    bool Create(const char* name, uint32_t slotCount, uint32_t slotSize);
    void Close();
    bool Write(const void* data, size_t size);

private:
    int fd_ = -1;
    void* map_ = nullptr;
    size_t size_ = 0;
    RingHeader* header_ = nullptr;
    uint8_t* payload_ = nullptr;
};

}
