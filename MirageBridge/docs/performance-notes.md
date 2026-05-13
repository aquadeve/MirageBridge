# Performance Notes

## Motion-To-Photon Budget

The target pipeline is:

```text
GVR predicted pose
  -> shared-memory pose packet
  -> client render
  -> client SBS submit or encoded frame
  -> Android present/decode
  -> Daydream async reprojection
  -> display scanout
```

For localhost Termux operation, keep the critical path on shared memory and avoid TCP. Use sockets only for control, fd handoff, and fallback.

## Allocation Strategy

- Keep packet structs fixed-size.
- Allocate large SBS scratch buffers once per runtime/service.
- Prefer ring reuse over per-frame heap allocation.
- Use newest-only reads for pose and video.
- Use bounded event queues.

## Frame Pacing

- HMD tracking loop: 200 Hz.
- Display loop: 72 Hz.
- Render against `predictedDisplayNs`.
- Drop old client frames instead of queueing latency.
- Keep command/control reliable, but video and pose should favor freshness.

## Snapdragon 835 Notes

- Prefer GLES3 pbuffer/FBO paths on Android 8.
- Keep CPU readback as a debug/fallback path only.
- Use MediaCodec low-latency H264 before H265 unless H265 latency is verified on-device.
- Avoid Java hot loops; Java should bootstrap lifecycle only.
- Pin heavy work to native threads and avoid blocking Binder/UI threads.

## Zero-Copy Upgrade Path

Current:

```text
GL FBO -> glReadPixels -> SBSFramePacket
```

Next:

```text
GL texture -> EGLImageKHR/AHardwareBuffer -> fd handoff -> texture import
```

Synchronization:

- EGL fence after producer render.
- fd/metadata handoff through Unix socket.
- consumer waits on fence before sampling.

## Diagnostics

Track:

- pose packet age
- predicted display delta
- frame submit age
- dropped ring writes
- socket reconnect count
- encode/decode duration
- audio underruns
- controller packet age

Existing tools:

- `pose-client`
- `sbs-frame-client`
- `mbr-pose-viewer`
- `mbr-submit-sbs`
- `mbr-protocol-smoke`

Recommended Android logcat tags:

```bash
adb logcat -s MirageBridgeCore MirageBridgeTrack MirageBridgeShm MirageBridgeSock MirageBridgeEGL
```
