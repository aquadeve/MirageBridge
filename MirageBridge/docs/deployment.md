# Deployment

## Android Build

```bash
cd MirageBridge
ANDROID_SDK_ROOT=/home/kantz/Android/Sdk ANDROID_HOME=/home/kantz/Android/Sdk bash scripts/build-android.sh
```

The Android project extracts local GVR headers and `libgvr.so` from:

```text
../gvr-android-sdk/libraries/sdk-base-1.190.0.aar
```

The app intentionally stays on GVR SDK `1.190.0` for Lenovo Mirage Solo firmware with
VRCore/Daydream OS-era services, including VRCore `1.23.283897185.release_aio`.

The APK is produced at:

```text
android/app/build/outputs/apk/debug/app-debug.apk
```

## Termux Build

Inside Termux:

```bash
pkg install clang cmake make
cd /sdcard/path/to/MirageBridge
bash scripts/build-termux.sh
```

On a normal Linux host the same CMake project also builds for compile testing:

```bash
cmake -S termux -B termux/build -DCMAKE_BUILD_TYPE=Release
cmake --build termux/build -j
ctest --test-dir termux/build --output-on-failure
```

## proot Ubuntu Runtime

Install the Termux prerequisites and Ubuntu rootfs:

```bash
pkg install proot-distro cmake clang make
proot-distro install ubuntu
```

Launch the proot build/runtime shell:

```bash
bash scripts/launch-proot-runtime.sh
```

The launcher enters Ubuntu, builds the Termux runtime targets, exports `XR_RUNTIME_JSON`, and starts `miragebridge-daemon` on `@miragebridge.termux`. Pass a command after the script to run an OpenXR app directly:

```bash
bash scripts/launch-proot-runtime.sh hello_xr -g OpenGLES
```

## Install And Run

Install and launch:

```bash
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n com.miragebridge/.MainActivity
```

Termux runtime order:

```bash
./termux/build/miragebridge-daemon/miragebridge-daemon
./termux/build/pose-client/pose-client
./termux/build/sbs-frame-client/sbs-frame-client
./termux/build/examples/mbr-pose-viewer
```

The daemon should start before the Android service for immediate fd handoff. If Android starts first, the native bridge retries the abstract socket connection from its tracking/frame loops, so the daemon can still attach after a short delay.

## Debugging

Android logs:

```bash
adb logcat -s MirageBridgeCore MirageBridgeTrack MirageBridgeShm MirageBridgeSock MirageBridgeEGL
```

Shared-memory health:

```bash
./pose-client
./sbs-frame-client
```

Expected early signals:

- `miragebridge-daemon listening on abstract socket @miragebridge.termux`
- `android bridge connected`
- `mapped Android shared rings`
- increasing pose frame ids
- increasing SBS frame ids

## OpenXR Runtime Experiments

The OpenXR runtime is built as:

```text
termux/build/openxr-shim/libopenxr_mirage.so
```

The CMake build emits a loader manifest with an absolute library path:

```text
termux/build/openxr-shim/openxr_mirage_runtime.json
```

Use it with the Khronos loader:

```bash
export XR_RUNTIME_JSON="$PWD/termux/build/openxr-shim/openxr_mirage_runtime.json"
XR_LOADER_DEBUG=all hello_xr -g OpenGL
```

On Mirage Solo/Termux, prefer the OpenGL ES path when the local `hello_xr` build supports it:

```bash
XR_LOADER_DEBUG=all hello_xr -g OpenGLES
```

Basic runtime checks:

```bash
nm -D termux/build/openxr-shim/libopenxr_mirage.so | grep xrNegotiateLoaderRuntimeInterface
openxr_runtime_list
```

Current support includes loader negotiation, instance/session lifecycle, stereo view configuration, reference spaces, OpenGL/OpenGL ES swapchain image handles, frame pacing, predicted view location, and projection-layer SBS submission back to Android. Controller action mapping, Vulkan, zero-copy buffers, and Android-side lens distortion/reprojection remain future backend work.

## Non-OpenXR SDK

Headers:

```text
sdk/include/mirage_runtime.h
sdk/include/mirage_runtime.hpp
```

Libraries:

```text
termux/build/sdk/libmirage_runtime.so
termux/build/sdk/libmirage_runtime.a
termux/build/sdk/vr.so
```

C/C++ examples:

```bash
./termux/build/examples/mbr-pose-viewer
./termux/build/examples/mbr-submit-sbs
```

Luau example:

```bash
LUA_CPATH="./termux/build/sdk/?.so;;" luau termux/examples/luau/pose_loop.luau
```
