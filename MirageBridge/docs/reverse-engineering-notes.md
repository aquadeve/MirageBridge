# Reverse-Engineering Notes

## Daydream/GVR Surface

The NDK sample establishes the safe public path:

- wrap the `gvr_context` with `gvr::GvrApi::WrapNonOwned`
- predict one display period ahead with `gvr::GvrApi::GetTimePointNow`
- call `GetHeadSpaceFromStartSpaceTransform`
- call `GetEyeFromHeadMatrix`
- call `SetToRecommendedBufferViewports`
- extract `BufferViewport::GetSourceFov`
- poll `gvr::ControllerApi` and `gvr::ControllerState`

MirageBridge follows that public API first. Hidden Daydream compositor interception is left out of phase 1 because it is fragile, likely SELinux-sensitive, and unnecessary for pose transport.

## Compositor And Frame Timing

The GVR sample renders into a GVR swapchain and submits to the Daydream compositor. MirageBridge deliberately does not capture the final compositor. It renders its own eye buffers and SBS output in an owned pbuffer context. This avoids protected compositor surfaces and gives Linux clients a deterministic test image.

The frame loop uses `13.888 ms`, matching 72 Hz. The tracking loop uses `5 ms`, matching the 200 Hz target. Packet timestamps are monotonic nanoseconds, compatible with Android `System.nanoTime()`/`CLOCK_MONOTONIC`.

## OpenXR Runtime Semantics

Monado's OpenXR state tracker was used as a behavioral reference for:

- `xrWaitFrame` returning predicted display time and period
- `xrBeginFrame`/`xrEndFrame` call-order expectations
- `XR_VIEW_STATE_ORIENTATION_VALID_BIT`
- `XR_VIEW_STATE_POSITION_VALID_BIT`
- tracked/valid view-state flags

The shim currently reports valid and tracked orientation/position whenever a packet is available.

## IPC And FD Passing

`xrtransport-referenceOnly` informed the socket-first control architecture: establish a Unix socket, exchange protocol metadata, and layer higher-level runtime calls over a transport boundary. MirageBridge specializes that idea for Android-to-Termux local transport:

- abstract Unix socket avoids app-private filesystem permissions
- `SCM_RIGHTS` passes Android shared-memory FDs into Termux
- Termux daemon mirrors into POSIX shm for ordinary local readers

## Native Symbols To Inspect On Device

Useful commands:

```bash
adb shell run-as com.miragebridge ls lib
adb shell pm path com.google.vr.vrcore
adb shell dumpsys activity service | grep -i vr
adb logcat -s VrCore GvrApi MirageBridgeCore MirageBridgeTrack
```

Likely Daydream services and libraries to investigate on a Mirage Solo:

- VrCore package services
- `libgvr.so`
- Daydream async reprojection service
- SurfaceFlinger layers during MirageBridge activity
- binder services containing `vr`, `gvr`, `daydream`, or `display`

## Future Root-Optional Work

- SurfaceFlinger layer inspection and frame timing correlation.
- `AHardwareBuffer` export and import into Termux-side native code.
- EGL fence synchronization instead of readback.
- Vulkan external memory experiments if Mesa/Vulkan are available in Termux.
- Binder service tracing for Daydream compositor timing.
