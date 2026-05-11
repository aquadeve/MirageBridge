#pragma once

#include <cstdint>

namespace miragebridge {

class FrameTimingDiagnostics {
public:
    void Reset();
    void MarkTrackingSample(uint64_t monotonicNs);
    void MarkSubmit(uint64_t monotonicNs);
    uint64_t LastTrackingNs() const;
    uint64_t LastSubmitNs() const;

private:
    uint64_t lastTrackingNs_ = 0;
    uint64_t lastSubmitNs_ = 0;
};

}
