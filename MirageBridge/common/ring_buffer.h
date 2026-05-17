#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "miragebridge_protocol.h"

namespace miragebridge {

inline void InitializeRing(void* map, uint32_t kind, uint32_t slotCount, uint32_t payloadSize) {
    std::memset(map, 0, RingBytes(slotCount, payloadSize));
    auto* header = new (map) RingHeader();
    header->magic = kProtocolMagic;
    header->version = kProtocolVersion;
    header->kind = kind;
    header->slotCount = slotCount;
    header->payloadSize = payloadSize;
    header->slotStride = RingSlotStride(payloadSize);
    header->writeSeq.store(0, std::memory_order_relaxed);
    header->readSeq.store(0, std::memory_order_relaxed);
    header->droppedWrites.store(0, std::memory_order_relaxed);

    uint8_t* payload = reinterpret_cast<uint8_t*>(map) + sizeof(RingHeader);
    const uint32_t stride = RingSlotStride(payloadSize);
    for (uint32_t i = 0; i < slotCount; ++i) {
        auto* slot = new (payload + static_cast<size_t>(i) * stride) RingSlotHeader();
        slot->beginSeq.store(0, std::memory_order_relaxed);
        slot->endSeq.store(0, std::memory_order_relaxed);
        slot->payloadBytes = 0;
        slot->flags = 0;
        slot->reserved0 = 0;
        slot->reserved1 = 0;
    }
}

inline uint8_t* RingPayloadBase(void* map) {
    return reinterpret_cast<uint8_t*>(map) + sizeof(RingHeader);
}

inline const uint8_t* RingPayloadBase(const void* map) {
    return reinterpret_cast<const uint8_t*>(map) + sizeof(RingHeader);
}

inline RingSlotHeader* RingSlot(RingHeader* header, uint8_t* payload, uint64_t sequence) {
    const uint32_t slot = static_cast<uint32_t>(sequence % header->slotCount);
    return reinterpret_cast<RingSlotHeader*>(payload + static_cast<size_t>(slot) * header->slotStride);
}

inline const RingSlotHeader* RingSlot(const RingHeader* header, const uint8_t* payload, uint64_t sequence) {
    const uint32_t slot = static_cast<uint32_t>(sequence % header->slotCount);
    return reinterpret_cast<const RingSlotHeader*>(payload + static_cast<size_t>(slot) * header->slotStride);
}

inline uint8_t* RingSlotPayload(RingSlotHeader* slot) {
    return reinterpret_cast<uint8_t*>(slot) + sizeof(RingSlotHeader);
}

inline const uint8_t* RingSlotPayload(const RingSlotHeader* slot) {
    return reinterpret_cast<const uint8_t*>(slot) + sizeof(RingSlotHeader);
}

inline bool RingWrite(RingHeader* header, uint8_t* payload, const void* data, size_t size) {
    if (!header || !payload || !data || size > header->payloadSize || header->slotCount == 0) {
        return false;
    }

    const uint64_t sequence = header->writeSeq.load(std::memory_order_relaxed);
    const uint64_t readSeq = header->readSeq.load(std::memory_order_acquire);
    if (sequence >= readSeq + header->slotCount) {
        header->droppedWrites.fetch_add(1, std::memory_order_relaxed);
    }
    RingSlotHeader* slot = RingSlot(header, payload, sequence);
    const uint64_t begin = sequence * 2 + 1;
    const uint64_t end = sequence * 2 + 2;

    slot->beginSeq.store(begin, std::memory_order_release);
    slot->payloadBytes = static_cast<uint32_t>(size);
    slot->flags = 0;
    std::memcpy(RingSlotPayload(slot), data, size);
    if (size < header->payloadSize) {
        std::memset(RingSlotPayload(slot) + size, 0, header->payloadSize - size);
    }
    slot->endSeq.store(end, std::memory_order_release);
    header->writeSeq.store(sequence + 1, std::memory_order_release);
    return true;
}

inline bool RingReadSequence(const RingHeader* header,
                             const uint8_t* payload,
                             uint64_t sequence,
                             void* outData,
                             size_t outSize,
                             uint32_t* outPayloadBytes = nullptr) {
    if (!header || !payload || !outData || outSize == 0 || header->slotCount == 0) {
        return false;
    }

    const RingSlotHeader* slot = RingSlot(header, payload, sequence);
    const uint64_t expectedBegin = sequence * 2 + 1;
    const uint64_t expectedEnd = sequence * 2 + 2;

    const uint64_t begin = slot->beginSeq.load(std::memory_order_acquire);
    const uint32_t payloadBytes = slot->payloadBytes;
    if (begin != expectedBegin || payloadBytes > outSize || payloadBytes > header->payloadSize) {
        return false;
    }

    std::memcpy(outData, RingSlotPayload(slot), payloadBytes);
    const uint64_t end = slot->endSeq.load(std::memory_order_acquire);
    if (end != expectedEnd || begin != slot->beginSeq.load(std::memory_order_acquire)) {
        return false;
    }

    if (outPayloadBytes) {
        *outPayloadBytes = payloadBytes;
    }
    return true;
}

inline bool RingReadLatest(const RingHeader* header,
                           const uint8_t* payload,
                           void* outData,
                           size_t outSize,
                           uint64_t* outSeq,
                           uint32_t* outPayloadBytes = nullptr) {
    if (!header || !payload) {
        return false;
    }

    const uint64_t writeSeq = header->writeSeq.load(std::memory_order_acquire);
    if (writeSeq == 0) {
        return false;
    }

    const uint64_t newest = writeSeq - 1;
    const uint64_t oldest = writeSeq > header->slotCount ? writeSeq - header->slotCount : 0;
    for (uint64_t sequence = newest + 1; sequence-- > oldest;) {
        if (RingReadSequence(header, payload, sequence, outData, outSize, outPayloadBytes)) {
            if (outSeq) {
                *outSeq = sequence;
            }
            const_cast<RingHeader*>(header)->readSeq.store(sequence + 1, std::memory_order_release);
            return true;
        }
        if (sequence == 0) {
            break;
        }
    }
    return false;
}

} 
