# Protocol

## Rings

MirageBridge uses two ring buffers:

- Tracking ring: `XRPacket`, 512 slots by default.
- Frame ring: `SBSFramePacket`, 8 slots by default.
- Client frame submission ring: `SBSFramePacket`, 4 slots by default.
- Audio out/in rings: `AudioPacket`, 64 slots by default.
- Runtime event ring: `RuntimeEventPacket`, 128 slots by default.

Android owns the first copy of headset-originated rings in `ASharedMemory`/ashmem. Termux receives those FDs and mirrors the latest data into POSIX shared memory so independent Termux processes can open them with `shm_open`. Client-originated rings are created by the SDK/daemon in POSIX shm and are reserved for frame/audio/input submission back to the Android service.

## Ring Header

`RingHeader` contains:

- magic/version/kind
- slot count
- payload size
- slot stride
- atomic write/read sequence counters
- dropped-write counter for future backpressure diagnostics

Each slot begins with `RingSlotHeader`:

- `beginSeq`: odd value while write is in progress
- `endSeq`: even value once write is complete
- `payloadBytes`

A reader accepts a slot only when begin/end match the expected sequence. This keeps the read path lock-free while avoiding torn packets.

## Tracking Packet

`XRPacket` contains:

- frame id
- current monotonic timestamp
- predicted display timestamp
- pose quaternion and position
- finite-difference angular and linear velocity
- two eye packets with eye transform, projection matrix, and OpenXR-style FOV
- optional controller state
- display and tracking rate metadata

Matrices are stored row-major to match GVR's `gvr_mat4f`. OpenGL users should transpose as needed.

## SBS Frame Packet

`SBSFramePacket` contains:

- `SBSFrameHeader`
- RGBA8888 side-by-side payload
- default dimensions: `2048 x 1024`
- left eye: left half
- right eye: right half

The current prototype reads pixels back into shared memory. A future zero-copy path should replace this with `AHardwareBuffer`/`EGLImageKHR` export once the receiving compositor path is ready.

## Socket Control Channel

The Android app connects to abstract Unix socket:

```text
@miragebridge.termux
```

Message types:

- `kSocketChunkSharedMemory`: sends two FDs and `SocketSegmentInfo` descriptors.
- `kSocketChunkTracking`: raw tracking fallback chunks.
- `kSocketChunkFrame`: raw SBS frame fallback chunks.
- `kSocketChunkClientFrame`: client-submitted SBS chunks.
- `kSocketChunkAudio`: PCM audio chunks.
- `kSocketChunkInput`: input/haptic commands.
- `kSocketChunkRuntimeEvent`: runtime events.

The control socket uses `SOCK_SEQPACKET` so each control/chunk message is preserved as a packet. Shared-memory FDs are transferred with `SCM_RIGHTS`.

## Runtime Command Surface

`RuntimeCommandPacket` is the base for command streams:

- `kRuntimeCommandSubmitSbsFrame`
- `kRuntimeCommandSubmitEncodedVideo`
- `kRuntimeCommandSubmitAudio`
- `kRuntimeCommandHapticPulse`
- `kRuntimeCommandSetBitrate`
- `kRuntimeCommandSetFramePacing`

The current SDK writes raw submitted SBS frames to `/miragebridge_client_frames` and PCM audio to `/miragebridge_audio_out`. Android-side consumption is the next backend step.
