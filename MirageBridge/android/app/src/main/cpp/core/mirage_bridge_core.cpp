#include "core/mirage_bridge_core.h"

#include <android/log.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include "core/frame_timing_diagnostics.h"
#include "core/gvr_tracking_adapter.h"
#include "core/shared_memory_transport.h"
#include "core/stereo_renderer.h"
#include "core/unix_socket_transport.h"

#define MB_LOG_TAG "MirageBridgeCore"
#define MB_LOGI(...) __android_log_print(ANDROID_LOG_INFO, MB_LOG_TAG, __VA_ARGS__)
#define MB_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, MB_LOG_TAG, __VA_ARGS__)

namespace miragebridge {
namespace {

std::atomic<bool> g_running{false};
std::thread g_trackingThread;
std::thread g_renderThread;
GvrTrackingAdapter g_tracking;
SharedMemoryTransport g_transport;
UnixSocketTransport g_socketFallback;
StereoRenderer g_renderer;
FrameTimingDiagnostics g_diag;
std::atomic<bool> g_handlesSent{false};

uint64_t MonotonicNs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

void MaybeSendSharedMemoryHandles() {
    if (!g_transport.IsReady() || g_handlesSent.load(std::memory_order_acquire)) {
        return;
    }
    const auto cfg = DefaultConfig();
    if (g_socketFallback.SendSharedMemoryHandles(g_transport.TrackingFd(),
                                                 g_transport.TrackingSize(),
                                                 g_transport.FrameFd(),
                                                 g_transport.FrameSize(),
                                                 cfg.trackingSlots,
                                                 cfg.frameSlots)) {
        g_handlesSent.store(true, std::memory_order_release);
    }
}

void TrackingLoop() {
    constexpr auto kTargetPeriod = std::chrono::microseconds(5000);
    auto nextWake = std::chrono::steady_clock::now();
    while (g_running.load(std::memory_order_relaxed)) {
        MaybeSendSharedMemoryHandles();
        XRPacket packet{};
        if (g_tracking.Poll(&packet)) {
            if (g_transport.IsReady()) {
                g_transport.PushTracking(packet);
            } else {
                g_socketFallback.SendTracking(packet);
            }
            g_diag.MarkTrackingSample(packet.monotonicNs);
        }
        nextWake += kTargetPeriod;
        std::this_thread::sleep_until(nextWake);
    }
}

void RenderLoop() {
    constexpr auto kDisplayPeriod = std::chrono::microseconds(13888);
    uint64_t frameId = 0;
    auto nextWake = std::chrono::steady_clock::now();
    auto frame = std::make_unique<SBSFramePacket>();
    while (g_running.load(std::memory_order_relaxed)) {
        MaybeSendSharedMemoryHandles();
        if (g_renderer.RenderAndPack(frameId++, frame.get())) {
            if (g_transport.IsReady()) {
                g_transport.PushFrame(*frame);
            } else {
                g_socketFallback.SendFrame(*frame);
            }
            g_diag.MarkSubmit(MonotonicNs());
        }
        nextWake += kDisplayPeriod;
        std::this_thread::sleep_until(nextWake);
    }
}

}

void StartBridge(JavaVM*, const char*, void* gvrContext) {
    if (g_running.exchange(true)) {
        return;
    }
    g_handlesSent.store(false, std::memory_order_release);

    const bool shmReady = g_transport.Initialize();
    if (!g_socketFallback.Initialize()) {
        MB_LOGE("UNIX socket control path not connected yet");
    }
    if (!shmReady) {
        MB_LOGE("Shared memory transport initialization failed, socket payload fallback will be used");
    }
    if (!g_tracking.Initialize(gvrContext)) {
        MB_LOGE("Tracking adapter initialization failed, using fallback simulation");
    }
    if (!g_renderer.Initialize()) {
        MB_LOGE("Stereo renderer initialization failed");
    }

    g_diag.Reset();
    g_trackingThread = std::thread(TrackingLoop);
    g_renderThread = std::thread(RenderLoop);
    MB_LOGI("MirageBridge started");
}

void StopBridge() {
    if (!g_running.exchange(false)) {
        return;
    }

    if (g_trackingThread.joinable()) {
        g_trackingThread.join();
    }
    if (g_renderThread.joinable()) {
        g_renderThread.join();
    }

    g_renderer.Shutdown();
    g_tracking.Shutdown();
    g_transport.Shutdown();
    g_socketFallback.Shutdown();
    MB_LOGI("MirageBridge stopped");
}

}
