#include "mirage_runtime.h"

#include <cstdint>
#include <cstring>

extern "C" {
struct lua_State;
typedef double lua_Number;
typedef long long lua_Integer;
typedef int (*lua_CFunction)(lua_State*);

void lua_createtable(lua_State* L, int narr, int nrec);
void lua_pushboolean(lua_State* L, int b);
void lua_pushcclosure(lua_State* L, lua_CFunction fn, int n);
void lua_pushinteger(lua_State* L, lua_Integer n);
void lua_pushnil(lua_State* L);
void lua_pushnumber(lua_State* L, lua_Number n);
void lua_pushstring(lua_State* L, const char* s);
void lua_setfield(lua_State* L, int idx, const char* k);
const char* luaL_optstring(lua_State* L, int narg, const char* def);
lua_Number luaL_optnumber(lua_State* L, int narg, lua_Number def);
}

namespace {

mbr_runtime* g_runtime = nullptr;

void PushFunction(lua_State* L, const char* name, lua_CFunction fn) {
    lua_pushcclosure(L, fn, 0);
    lua_setfield(L, -2, name);
}

void PushVec3(lua_State* L, const char* name, const mbr_vec3& v) {
    lua_createtable(L, 0, 3);
    lua_pushnumber(L, v.x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, v.y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, v.z);
    lua_setfield(L, -2, "z");
    lua_setfield(L, -2, name);
}

void PushQuat(lua_State* L, const char* name, const mbr_quat& q) {
    lua_createtable(L, 0, 4);
    lua_pushnumber(L, q.x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, q.y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, q.z);
    lua_setfield(L, -2, "z");
    lua_pushnumber(L, q.w);
    lua_setfield(L, -2, "w");
    lua_setfield(L, -2, name);
}

int Connect(lua_State* L) {
    const char* endpoint = luaL_optstring(L, 1, "local");
    if (!g_runtime) {
        mbr_runtime_config cfg{};
        cfg.application_name = "luau";
        if (mbr_runtime_create(&cfg, &g_runtime) != MBR_SUCCESS) {
            lua_pushboolean(L, 0);
            return 1;
        }
    }
    lua_pushboolean(L, mbr_runtime_connect(g_runtime, endpoint) == MBR_SUCCESS ? 1 : 0);
    return 1;
}

int Disconnect(lua_State*) {
    if (g_runtime) {
        mbr_runtime_disconnect(g_runtime);
    }
    return 0;
}

int Running(lua_State* L) {
    lua_pushboolean(L, mbr_runtime_is_connected(g_runtime));
    return 1;
}

int WaitFrame(lua_State* L) {
    const uint64_t timeoutNs = static_cast<uint64_t>(luaL_optnumber(L, 1, 0.0));
    mbr_frame_timing timing{};
    if (mbr_runtime_wait_frame(g_runtime, timeoutNs, &timing) != MBR_SUCCESS) {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 4);
    lua_pushinteger(L, static_cast<lua_Integer>(timing.frame_id));
    lua_setfield(L, -2, "frameId");
    lua_pushinteger(L, static_cast<lua_Integer>(timing.predicted_display_ns));
    lua_setfield(L, -2, "predictedDisplayNs");
    lua_pushinteger(L, static_cast<lua_Integer>(timing.display_period_ns));
    lua_setfield(L, -2, "displayPeriodNs");
    lua_pushboolean(L, timing.should_render);
    lua_setfield(L, -2, "shouldRender");
    return 1;
}

int GetPose(lua_State* L) {
    mbr_headset_state state{};
    if (mbr_runtime_get_headset_state(g_runtime, &state) != MBR_SUCCESS) {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 6);
    lua_pushinteger(L, static_cast<lua_Integer>(state.frame_id));
    lua_setfield(L, -2, "frameId");
    lua_pushinteger(L, static_cast<lua_Integer>(state.pose.timestamp_ns));
    lua_setfield(L, -2, "timestampNs");
    lua_pushinteger(L, static_cast<lua_Integer>(state.pose.predicted_display_ns));
    lua_setfield(L, -2, "predictedDisplayNs");
    PushVec3(L, "position", state.pose.position);
    PushQuat(L, "rotation", state.pose.rotation);
    PushVec3(L, "linearVelocity", state.pose.linear_velocity);
    PushVec3(L, "angularVelocity", state.pose.angular_velocity);
    return 1;
}

int GetController(lua_State* L) {
    const uint32_t index = static_cast<uint32_t>(luaL_optnumber(L, 1, 0.0));
    mbr_controller_state state{};
    if (mbr_runtime_get_controller_state(g_runtime, index, &state) != MBR_SUCCESS) {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 7);
    lua_pushinteger(L, state.id);
    lua_setfield(L, -2, "id");
    lua_pushboolean(L, state.connected);
    lua_setfield(L, -2, "connected");
    lua_pushinteger(L, state.buttons);
    lua_setfield(L, -2, "buttons");
    lua_pushnumber(L, state.trigger);
    lua_setfield(L, -2, "trigger");
    PushVec3(L, "position", state.pose.position);
    PushQuat(L, "rotation", state.pose.rotation);
    return 1;
}

int PollEvents(lua_State* L) {
    mbr_event event{};
    uint32_t count = 0;
    if (mbr_runtime_poll_events(g_runtime, &event, 1, &count) != MBR_SUCCESS || count == 0) {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 5);
    lua_pushinteger(L, static_cast<lua_Integer>(event.id));
    lua_setfield(L, -2, "id");
    lua_pushinteger(L, event.type);
    lua_setfield(L, -2, "type");
    lua_pushinteger(L, event.code);
    lua_setfield(L, -2, "code");
    lua_pushinteger(L, static_cast<lua_Integer>(event.value));
    lua_setfield(L, -2, "value");
    lua_pushinteger(L, static_cast<lua_Integer>(event.timestamp_ns));
    lua_setfield(L, -2, "timestampNs");
    return 1;
}

int Metrics(lua_State* L) {
    mbr_metrics metrics{};
    if (mbr_runtime_get_metrics(g_runtime, &metrics) != MBR_SUCCESS) {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 7);
    lua_pushinteger(L, static_cast<lua_Integer>(metrics.pose_packets_read));
    lua_setfield(L, -2, "posePacketsRead");
    lua_pushinteger(L, static_cast<lua_Integer>(metrics.frame_packets_read));
    lua_setfield(L, -2, "framePacketsRead");
    lua_pushinteger(L, static_cast<lua_Integer>(metrics.frames_submitted));
    lua_setfield(L, -2, "framesSubmitted");
    lua_pushinteger(L, static_cast<lua_Integer>(metrics.audio_packets_submitted));
    lua_setfield(L, -2, "audioPacketsSubmitted");
    lua_pushinteger(L, static_cast<lua_Integer>(metrics.dropped_events));
    lua_setfield(L, -2, "droppedEvents");
    return 1;
}

} 

extern "C" int luaopen_vr(lua_State* L) {
    lua_createtable(L, 0, 8);
    PushFunction(L, "connect", Connect);
    PushFunction(L, "disconnect", Disconnect);
    PushFunction(L, "running", Running);
    PushFunction(L, "waitFrame", WaitFrame);
    PushFunction(L, "getPose", GetPose);
    PushFunction(L, "getController", GetController);
    PushFunction(L, "pollEvents", PollEvents);
    PushFunction(L, "metrics", Metrics);
    return 1;
}
