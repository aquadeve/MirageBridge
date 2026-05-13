#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#if defined(MIRAGE_RUNTIME_BUILD)
#define MBR_API __declspec(dllexport)
#else
#define MBR_API __declspec(dllimport)
#endif
#else
#define MBR_API __attribute__((visibility("default")))
#endif

typedef struct mbr_runtime mbr_runtime;

typedef enum mbr_result {
    MBR_SUCCESS = 0,
    MBR_ERROR_INVALID_ARGUMENT = -1,
    MBR_ERROR_NOT_CONNECTED = -2,
    MBR_ERROR_TRANSPORT_UNAVAILABLE = -3,
    MBR_ERROR_TIMEOUT = -4,
    MBR_ERROR_UNSUPPORTED = -5,
    MBR_ERROR_BUFFER_TOO_SMALL = -6,
    MBR_ERROR_INTERNAL = -7
} mbr_result;

typedef enum mbr_transport_mode {
    MBR_TRANSPORT_AUTO = 0,
    MBR_TRANSPORT_LOCAL_SHM = 1,
    MBR_TRANSPORT_UNIX_SOCKET = 2,
    MBR_TRANSPORT_UDP = 3
} mbr_transport_mode;

typedef enum mbr_event_type {
    MBR_EVENT_NONE = 0,
    MBR_EVENT_CONNECTED = 1,
    MBR_EVENT_DISCONNECTED = 2,
    MBR_EVENT_POSE = 3,
    MBR_EVENT_FRAME = 4,
    MBR_EVENT_CONTROLLER = 5,
    MBR_EVENT_AUDIO_UNDERRUN = 6,
    MBR_EVENT_TRANSPORT_WARNING = 7
} mbr_event_type;

typedef enum mbr_pixel_format {
    MBR_PIXEL_FORMAT_RGBA8 = 1,
    MBR_PIXEL_FORMAT_BGRA8 = 2
} mbr_pixel_format;

typedef enum mbr_audio_format {
    MBR_AUDIO_FORMAT_F32 = 1,
    MBR_AUDIO_FORMAT_S16 = 2
} mbr_audio_format;

typedef struct mbr_vec2 {
    float x;
    float y;
} mbr_vec2;

typedef struct mbr_vec3 {
    float x;
    float y;
    float z;
} mbr_vec3;

typedef struct mbr_quat {
    float x;
    float y;
    float z;
    float w;
} mbr_quat;

typedef struct mbr_pose {
    mbr_vec3 position;
    mbr_quat rotation;
    mbr_vec3 linear_velocity;
    mbr_vec3 angular_velocity;
    uint64_t timestamp_ns;
    uint64_t predicted_display_ns;
} mbr_pose;

typedef struct mbr_eye_view {
    float view[16];
    float projection[16];
    float fov[4];
} mbr_eye_view;

typedef struct mbr_headset_state {
    uint64_t frame_id;
    mbr_pose pose;
    mbr_eye_view eyes[2];
    uint32_t display_width;
    uint32_t display_height;
    uint32_t display_hz;
    uint32_t tracking_hz;
} mbr_headset_state;

typedef struct mbr_controller_state {
    uint32_t id;
    uint32_t connected;
    uint32_t buttons;
    float trigger;
    mbr_vec2 touchpad;
    mbr_pose pose;
} mbr_controller_state;

typedef struct mbr_frame_timing {
    uint64_t frame_id;
    uint64_t predicted_display_ns;
    uint64_t display_period_ns;
    uint32_t should_render;
} mbr_frame_timing;

typedef struct mbr_sbs_frame_desc {
    uint64_t frame_id;
    uint64_t target_display_ns;
    uint32_t width;
    uint32_t height;
    uint32_t stride_bytes;
    uint32_t pixel_format;
    const void* pixels;
    uint32_t bytes;
} mbr_sbs_frame_desc;

typedef struct mbr_audio_desc {
    uint64_t packet_id;
    uint64_t timestamp_ns;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t frame_count;
    uint32_t format;
    const void* samples;
    uint32_t bytes;
} mbr_audio_desc;

typedef struct mbr_event {
    uint64_t id;
    uint64_t timestamp_ns;
    uint32_t type;
    uint32_t code;
    uint64_t value;
    float data[8];
} mbr_event;

typedef struct mbr_metrics {
    uint64_t pose_packets_read;
    uint64_t frame_packets_read;
    uint64_t frames_submitted;
    uint64_t audio_packets_submitted;
    uint64_t dropped_events;
    uint64_t last_pose_ns;
    uint64_t last_submit_ns;
} mbr_metrics;

typedef struct mbr_runtime_config {
    const char* application_name;
    const char* endpoint;
    uint32_t transport_mode;
    uint32_t flags;
} mbr_runtime_config;

MBR_API const char* mbr_result_to_string(mbr_result result);
MBR_API mbr_result mbr_runtime_create(const mbr_runtime_config* config, mbr_runtime** out_runtime);
MBR_API void mbr_runtime_destroy(mbr_runtime* runtime);
MBR_API mbr_result mbr_runtime_connect(mbr_runtime* runtime, const char* endpoint);
MBR_API void mbr_runtime_disconnect(mbr_runtime* runtime);
MBR_API uint32_t mbr_runtime_is_connected(const mbr_runtime* runtime);
MBR_API mbr_result mbr_runtime_poll_events(mbr_runtime* runtime, mbr_event* events, uint32_t capacity, uint32_t* out_count);
MBR_API mbr_result mbr_runtime_wait_frame(mbr_runtime* runtime, uint64_t timeout_ns, mbr_frame_timing* out_timing);
MBR_API mbr_result mbr_runtime_get_headset_state(mbr_runtime* runtime, mbr_headset_state* out_state);
MBR_API mbr_result mbr_runtime_get_controller_state(mbr_runtime* runtime, uint32_t index, mbr_controller_state* out_state);
MBR_API mbr_result mbr_runtime_get_latest_sbs_frame(mbr_runtime* runtime, mbr_sbs_frame_desc* out_desc, void* pixel_buffer, uint32_t pixel_buffer_bytes, uint32_t* out_bytes);
MBR_API mbr_result mbr_runtime_submit_sbs_frame(mbr_runtime* runtime, const mbr_sbs_frame_desc* frame);
MBR_API mbr_result mbr_runtime_submit_audio(mbr_runtime* runtime, const mbr_audio_desc* audio);
MBR_API mbr_result mbr_runtime_get_metrics(const mbr_runtime* runtime, mbr_metrics* out_metrics);

#ifdef __cplusplus
}
#endif
