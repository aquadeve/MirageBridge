# MirageBridge Core

MirageBridge is a research-grade prototype for turning a Lenovo Mirage Solo into a local Linux-accessible XR device. The Android side runs a foreground native bridge service against the Daydream/GVR runtime; the Termux side receives tracking and frame data, mirrors it into POSIX shared memory, and exposes small client tools plus a prototype OpenXR-facing shim.
