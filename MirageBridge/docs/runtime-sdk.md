# Mirage Runtime SDK

The Mirage Runtime SDK is the non-OpenXR application interface for MirageBridge. It is designed for native games, custom engines, Termux tools, and embedded language hosts that want VR pose/input/frame/audio access without adopting OpenXR.

## Goals

- Small C ABI that can be bound from C, C++, Rust, Go, Python, Zig, Java, Kotlin, Lua, and Luau.
- No OpenXR dependency in client applications.
- Localhost-first transport optimized for Android + Termux.
- Replaceable transport backends: POSIX shm, Unix socket, UDP streaming.
- Deterministic frame timing and low allocation pressure.
- Script-first API through Luau.

## Public Artifacts

```text
sdk/include/mirage_runtime.h       C ABI
sdk/include/mirage_runtime.hpp     C++ RAII wrapper
termux/build/sdk/libmirage_runtime.so
termux/build/sdk/libmirage_runtime.a
termux/build/sdk/vr.so             Luau native module
```

## Source Tree Additions

```text
termux/sdk/
  src/mirage_runtime.cpp           C ABI implementation
  luau/vr_luau.cpp                 Luau module bridge
termux/examples/
  c/pose_viewer.c                  Minimal C pose loop
  cpp/submit_sbs.cpp               Client SBS frame submission
  luau/pose_loop.luau              Scripted pose loop
termux/tests/
  protocol_smoke.cpp               Ring/protocol smoke test
```

## Runtime Lifecycle

```c
mbr_runtime_config cfg = {0};
cfg.application_name = "my-engine";

mbr_runtime* rt = NULL;
mbr_runtime_create(&cfg, &rt);
mbr_runtime_connect(rt, "local");

while (mbr_runtime_is_connected(rt)) {
    mbr_frame_timing timing;
    if (mbr_runtime_wait_frame(rt, 1000000000ull, &timing) == MBR_SUCCESS) {
        mbr_headset_state hmd;
        mbr_runtime_get_headset_state(rt, &hmd);
    }
}

mbr_runtime_destroy(rt);
```

## Data Model

- `mbr_headset_state`: latest HMD pose, eye views, projections, FOV, display rate.
- `mbr_controller_state`: generic Daydream controller pose/buttons/touchpad/trigger.
- `mbr_frame_timing`: predicted display time and period.
- `mbr_sbs_frame_desc`: raw stereo side-by-side RGBA frame submission.
- `mbr_audio_desc`: PCM audio submission scaffold.
- `mbr_event`: async runtime event surface.
- `mbr_metrics`: low-overhead counters for diagnostics.

## C ABI Stability Rules

- Public structs use fixed-width integer and float fields only.
- No STL or C++ types cross the ABI.
- Functions return `mbr_result`; strings are diagnostic only.
- Opaque handles are owned by `mbr_runtime_create`/`mbr_runtime_destroy`.
- Future fields should be added through new structs or versioned extension functions.

## Language Embedding Notes

- Rust: bind with `bindgen` or hand-write `extern "C"` declarations.
- Go: use cgo against `mirage_runtime.h`.
- Python: use `ctypes` or `cffi`.
- Zig: `@cImport` the C header.
- Java/Kotlin: use JNI or JNA on desktop Linux; on Android prefer direct NDK integration.
- Lua/Luau: load `vr.so` or wrap `mirage_runtime.h` from a host-specific FFI layer.

## Current Backend Status

Implemented:

- POSIX shm client transport.
- Tracking, frame readback, controller query.
- Client SBS frame submission ring.
- Audio submission ring scaffold.
- Event queue and metrics.
- C, C++, and Luau-facing API.

Specified/scaffolded:

- UDP low-latency streaming mode.
- H264/H265 packetization.
- Audio capture/microphone forwarding.
- Android-side compositor consumption of client-submitted frames.
- Future OpenXR compatibility layer on top of the C ABI.
