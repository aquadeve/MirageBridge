# Architecture

## Data Flow

```text
Daydream/GVR runtime
  -> GvrTrackingAdapter
  -> XRPacket ring in Android ASharedMemory/ashmem
  -> abstract Unix socket fd handoff
  -> miragebridge-daemon in Termux
  -> POSIX shm mirror
  -> pose-client / openxr-shim

StereoRenderer
  -> left eye FBO + right eye FBO
  -> SBS FBO
  -> SBSFramePacket ring in Android ASharedMemory/ashmem
  -> miragebridge-daemon
  -> POSIX shm mirror
  -> sbs-frame-client / future compositor
```

## Android Components

- `MainActivity` owns `GvrLayout`, enables VR mode and async reprojection, then starts the foreground bridge service with the native `gvr_context`.
- `MirageBridgeService` keeps the bridge foreground, holds a partial wake lock, and starts/stops the native core.
- `NativeTrackingCore` is represented by `MirageBridgeCore`: it owns the tracking and frame threads.
- `GvrTrackingAdapter` wraps the non-owned GVR context and extracts predicted head pose, eye transforms, projection matrices, FOV, controller state, and finite-difference velocities.
- `SharedMemoryTransport` creates the Android rings using `ASharedMemory_create()` on API 26+ with `/dev/ashmem` fallback.
- `UnixSocketTransport` connects to Termux through abstract socket `@miragebridge.termux`, sends shared-memory FDs with `SCM_RIGHTS`, and can chunk raw packets if shared memory fails.
- `EGLCapturePipeline` owns a pbuffer EGL context and renderable GLES3 FBOs.
- `StereoRenderer` packs the offscreen render into `SBSFramePacket`.

## Termux Components

- `miragebridge-daemon` binds the abstract socket, receives Android FDs, maps the remote rings read-only, and mirrors tracking/frame data into POSIX shm names `/miragebridge_tracking` and `/miragebridge_frames`.
- `pose-client` reads the latest `XRPacket` and prints pose, yaw, and velocity as a text pose visualizer.
- `sbs-frame-client` reads the latest SBS frame header and first pixel for transport sanity checks.
- `openxr-shim` exposes `xrWaitFrame`, `xrBeginFrame`, `xrEndFrame`, `xrLocateViews`, and a minimal `xrGetInstanceProcAddr` dispatch surface.

## Threading And Timing

- Tracking loop target: 200 Hz (`5 ms` period).
- Frame loop target: 72 Hz (`13.888 ms` period).
- GVR prediction horizon: one 72 Hz display period.
- The shared-memory ring is single-writer/multi-reader friendly. Each slot has begin/end sequence values so readers can reject torn packets without locks.

## Reference Material Used

- `gvr-android-sdk/samples/ndk-hellovr`: GVR non-owned context wrapping, `GetHeadSpaceFromStartSpaceTransform`, recommended viewports, eye matrices, controller polling, async reprojection.
- `monado-referenceOnly/src/xrt/state_trackers/oxr`: OpenXR frame call ordering and view-state flag semantics.
- `xrtransport-referenceOnly/src/server` and `include/xrtransport`: Unix socket setup, protocol handshakes, and transport/module layering patterns.
