# OpenXR Runtime Bring-Up

`libopenxr_mirage.so` is a minimal OpenXR runtime for loader compatibility and hello_xr bring-up. It is not the primary MirageBridge application API; native apps should still prefer `mirage_runtime` or the Luau module when they do not need OpenXR.

## Loader ABI

The runtime includes the Khronos loader negotiation header:

```cpp
#include <openxr/openxr_loader_negotiation.h>
```

It exports:

```text
xrNegotiateLoaderRuntimeInterface
xrGetInstanceProcAddr
```

Negotiation validates the loader info struct, interface version range, and OpenXR API range, then returns interface version `1`, API version `1.0.0`, and the runtime `xrGetInstanceProcAddr`.

The shared object is linked with `-Wl,-Bsymbolic-functions` on Unix builds. This is important for loader compatibility: without local symbol binding, ELF interposition can resolve runtime references such as `xrGetInstanceProcAddr` back to the loader trampoline, causing recursive loader-lock deadlocks during extension enumeration.

## Dispatch Surface

`xrGetInstanceProcAddr` resolves the core functions needed by hello_xr:

```text
xrCreateInstance
xrDestroyInstance
xrGetSystem
xrCreateSession
xrDestroySession
xrBeginSession
xrEndSession
xrPollEvent
xrCreateReferenceSpace
xrEnumerateViewConfigurations
xrEnumerateViewConfigurationViews
xrEnumerateEnvironmentBlendModes
xrCreateSwapchain
xrDestroySwapchain
xrAcquireSwapchainImage
xrWaitSwapchainImage
xrReleaseSwapchainImage
xrWaitFrame
xrBeginFrame
xrEndFrame
xrLocateViews
```

It also exposes OpenGL/OpenGL ES graphics requirement functions, path helpers, result/type string helpers, and no-op action APIs so simple samples avoid immediate `XR_ERROR_FUNCTION_UNSUPPORTED` failures.

## Manifest

CMake writes the loader manifest next to the shared object:

```text
termux/build/openxr-shim/openxr_mirage_runtime.json
```

The generated manifest uses the required schema and an absolute `library_path`:

```json
{
  "file_format_version": "1.0.0",
  "runtime": {
    "name": "MirageBridge OpenXR",
    "library_path": "/absolute/path/libopenxr_mirage.so"
  }
}
```

## Termux Test Flow

```bash
cmake -S termux -B termux/build -DCMAKE_BUILD_TYPE=Release
cmake --build termux/build -j
ctest --test-dir termux/build --output-on-failure

nm -D termux/build/openxr-shim/libopenxr_mirage.so | grep xrNegotiateLoaderRuntimeInterface
export XR_RUNTIME_JSON="$PWD/termux/build/openxr-shim/openxr_mirage_runtime.json"
XR_LOADER_DEBUG=all hello_xr -g OpenGL
```

On ARM64 Termux, confirm the target after building on-device:

```bash
readelf -h termux/build/openxr-shim/libopenxr_mirage.so | grep Machine
```

Expected value on Mirage Solo/Termux is `AArch64`. Host Linux compile tests may show `Advanced Micro Devices X86-64`; that only validates source and loader ABI behavior for the host build.

## Runtime Behavior

- System: one HMD system with primary stereo view configuration.
- Tracking: reads `/miragebridge_tracking`; falls back to identity orientation at standing height when the Android service is not connected.
- Timing: reports a 72 Hz predicted display period.
- Swapchains: returns OpenGL/OpenGL ES texture ids. If no GL context is current during smoke tests, placeholder ids are returned so lifecycle tests can proceed.
- Events: emits READY after session creation, FOCUSED after begin session, and STOPPING/IDLE during end-session flow.

## Remaining Work

- Real OpenGL ES texture ownership and import/export on Termux.
- Android Daydream compositor submission instead of local placeholder swapchain ownership.
- Controller action state mapped from MirageBridge controller packets.
- Vulkan graphics binding and external memory paths.
- Runtime conformance, loader edge-case validation, and robust multi-instance cleanup.
