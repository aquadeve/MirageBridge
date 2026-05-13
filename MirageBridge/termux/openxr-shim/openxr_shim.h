#pragma once

#define XR_USE_GRAPHICS_API_OPENGL 1
#define XR_USE_GRAPHICS_API_OPENGL_ES 1

// openxr_platform.h exposes XR_FB_swapchain_update_state_opengl_es whenever
// XR_USE_GRAPHICS_API_OPENGL_ES is enabled. That optional struct needs EGLenum
// even for Linux/Termux builds that only use generic OpenGLES swapchain image
// handles and do not include Android EGL platform bindings.
typedef unsigned int EGLenum;

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_loader_negotiation.h>

extern "C" {
XRAPI_ATTR XrResult XRAPI_CALL xrNegotiateLoaderRuntimeInterface(
    const XrNegotiateLoaderInfo* loaderInfo,
    XrNegotiateRuntimeRequest* runtimeRequest);

XRAPI_ATTR XrResult XRAPI_CALL xrGetInstanceProcAddr(
    XrInstance instance,
    const char* name,
    PFN_xrVoidFunction* function);
}
