#include "core/frame_timing_diagnostics.h"

namespace miragebridge {

void FrameTimingDiagnostics::Reset() {
    lastTrackingNs_ = 0;
    lastSubmitNs_ = 0;
}

void FrameTimingDiagnostics::MarkTrackingSample(uint64_t monotonicNs) {
    lastTrackingNs_ = monotonicNs;
}

void FrameTimingDiagnostics::MarkSubmit(uint64_t monotonicNs) {
    lastSubmitNs_ = monotonicNs;
}

uint64_t FrameTimingDiagnostics::LastTrackingNs() const {
    return lastTrackingNs_;
}

uint64_t FrameTimingDiagnostics::LastSubmitNs() const {
    return lastSubmitNs_;
}

}
