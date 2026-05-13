# MirageBridge

MirageBridge is a research-grade prototype for turning a Lenovo Mirage Solo into a local Linux-accessible XR device. The Android side runs a foreground native bridge service against the Daydream/GVR runtime; the Termux side receives tracking and frame data, mirrors it into POSIX shared memory, and exposes small client tools plus a prototype OpenXR-facing shim.

## What Is Implemented

- Android foreground service bootstrap with minimal Java overhead.
- GVR/Daydream tracking adapter for head pose, eye transforms, projection matrices, prediction time, and optional controller state.
- 200 Hz native tracking loop and 72 Hz SBS frame loop.
- `ASharedMemory`/ashmem ring buffers with per-slot sequence guards.
- Abstract Unix domain socket control channel with `SCM_RIGHTS` fd handoff.
- Chunked socket payload fallback if shared memory cannot initialize.
- Offscreen EGL/GLES3 pbuffer renderer that renders left and right eye FBOs and blits them into an SBS frame buffer.
- Termux daemon that maps Android shared-memory FDs and mirrors them into POSIX shm for local readers.
- `pose-client`, `sbs-frame-client`, and a minimal Khronos-loader-compatible `libopenxr_mirage.so` runtime.
- Non-OpenXR embedding SDK with C ABI, C++ wrapper, native Luau module, examples, and smoke tests.

## Repository Layout

```text
MirageBridge/
  android/                 Android app and native bridge service
  common/                  Binary protocol and shared-memory ring helpers
  termux/                  Daemon, clients, and OpenXR shim
  sdk/include/             Public C/C++ embedding SDK headers
  scripts/                 Build/deploy helpers
  docs/                    Architecture, protocol, deployment, and RE notes
```

The root-level `monado-referenceOnly/` and `xrtransport-referenceOnly/` folders were used as read-only references for OpenXR frame semantics, view-state flags, transport handshakes, Unix socket setup, and fd-passing architecture. The GVR SDK sample was used for Daydream pose, viewport, reprojection, and controller API patterns.

## Quick Build

```bash
cd MirageBridge
ANDROID_SDK_ROOT=/home/kantz/Android/Sdk ANDROID_HOME=/home/kantz/Android/Sdk bash scripts/build-android.sh
bash scripts/build-termux.sh
```

Outputs:

- Android APK: `android/app/build/outputs/apk/debug/app-debug.apk`
- Termux binaries: `termux/build/miragebridge-daemon/miragebridge-daemon`, `pose-client`, `sbs-frame-client`
- OpenXR runtime: `termux/build/openxr-shim/libopenxr_mirage.so`
- OpenXR manifest: `termux/build/openxr-shim/openxr_mirage_runtime.json`
- Runtime SDK: `termux/build/sdk/libmirage_runtime.so`, `libmirage_runtime.a`, `vr.so`
- SDK examples: `termux/build/examples/mbr-pose-viewer`, `mbr-submit-sbs`

## Runtime Order

1. Start Termux and run `miragebridge-daemon`.
2. Install and launch the Android APK.
3. Run `pose-client` or `sbs-frame-client` in Termux.
4. For non-OpenXR applications, link `libmirage_runtime.so` or use the single C header in `sdk/include/mirage_runtime.h`.
5. For Luau hosts, add `termux/build/sdk` to the native module search path and `require("vr")`.
6. For OpenXR experiments, set `XR_RUNTIME_JSON=$PWD/termux/build/openxr-shim/openxr_mirage_runtime.json` and run `hello_xr -g OpenGL` or the local OpenGL ES variant when available.

MirageBridge uses an abstract Android Unix socket named `@miragebridge.termux`, so it does not depend on Termux private app-data paths or root-only filesystem access.

## Prototype Boundaries

This is not a full Khronos-conformant OpenXR runtime yet. The primary public SDK is intentionally non-OpenXR: it is a lightweight C ABI plus Luau-facing runtime for embedded engines. `libopenxr_mirage.so` now negotiates with the Khronos loader and can bring up minimal instance/session/frame/swapchain flow, but real compositor presentation, hardware video encode, audio routing, Vulkan external memory, and Android-side presentation of client-submitted frames still need on-device backend work.

Code is most importantly made with AI and the openxr shim needs a lot more fixes!!!
