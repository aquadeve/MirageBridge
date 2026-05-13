# Networking And IPC Specification

MirageBridge has three transport tiers. All use the same packet model from `common/miragebridge_protocol.h`.

## Tier 0: Local Shared Memory

Best for Termux on the same Android device.

- Android creates `ASharedMemory`/ashmem rings.
- Android passes FDs to `miragebridge-daemon` over abstract Unix socket.
- Termux daemon mirrors to POSIX shm:
  - `/miragebridge_tracking`
  - `/miragebridge_frames`
  - `/miragebridge_client_frames`
  - `/miragebridge_audio_out`
  - `/miragebridge_audio_in`
  - `/miragebridge_events`
- SDK reads/writes POSIX shm directly.

Latency profile:

- pose read: one memcpy of `XRPacket`
- SBS read: one optional memcpy of frame payload
- client submit: one memcpy into submit ring

## Tier 1: Unix Socket

Best for localhost command/control, fd handoff, fallback payloads, and future bidirectional frame relay.

Socket:

```text
AF_UNIX SOCK_SEQPACKET @miragebridge.termux
```

Message types:

- `kSocketChunkSharedMemory`: fd handoff with `SCM_RIGHTS`
- `kSocketChunkTracking`: fallback tracking payload
- `kSocketChunkFrame`: fallback SBS frame payload
- `kSocketChunkClientFrame`: client-submitted SBS frame
- `kSocketChunkAudio`: PCM audio payload
- `kSocketChunkInput`: controller/input command
- `kSocketChunkRuntimeEvent`: runtime event

Large payloads are split with `SocketPayloadHeader`:

- message id
- total bytes
- offset
- payload bytes

## Tier 2: UDP Streaming

Best for optional Wi-Fi streaming.

Planned channels:

- reliable TCP/Unix command channel
- unreliable UDP pose/video/audio data channel
- optional FEC/retransmit for video keyframes

Packet classes:

- pose sync
- encoded video fragment
- audio packet
- input event
- time sync
- bitrate control

Recommended defaults:

- pose: 200 Hz, newest-only
- video: 72 Hz, H264/H265 low-latency mode
- audio: 48 kHz, 10-20 ms packets
- command: reliable, ordered

## Time Synchronization

All timestamps are monotonic nanoseconds. On localhost, Android and Termux share the same kernel monotonic clock. For Wi-Fi mode, add a ping/pong clock sync:

```text
client_send_ns
server_recv_ns
server_send_ns
client_recv_ns
```

The client estimates offset and jitter, then submits frames tagged with target display time.

## Pose Prediction

The Android side publishes:

- sample timestamp
- predicted display timestamp
- display period
- pose
- velocity estimates

Clients should render against `predictedDisplayNs` from `mbr_frame_timing`. If a client misses the target, it should submit anyway and allow Android-side reprojection/timewarp to pick the newest usable frame.

## Video Streaming Pipeline

Planned Android-side pipeline:

```text
client GL/Vulkan render
  -> SBS RGBA ring or encoder input
  -> H264/H265 low-latency encoder
  -> UDP/video packetizer
  -> Android MediaCodec decoder
  -> GL texture
  -> Daydream swapchain/compositor
```

Localhost zero-copy target:

```text
AHardwareBuffer/EGLImageKHR
  -> fd handoff
  -> Android GLES texture import
```

The current implementation provides raw SBS frame rings first because they are deterministic and easy to inspect.

## Audio Pipeline

Planned channels:

- client-to-headset playback: SDK `mbr_runtime_submit_audio`
- headset-to-client microphone: Android AudioRecord/OpenSL ES capture into `/miragebridge_audio_in`
- Linux integration: PulseAudio/PipeWire bridge process reads/writes SDK audio rings

Audio packet format:

- sample rate
- channel count
- frame count
- format
- monotonic timestamp
- PCM payload
