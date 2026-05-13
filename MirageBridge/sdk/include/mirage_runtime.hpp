#pragma once

#include <stdexcept>
#include <string>

#include "mirage_runtime.h"

namespace miragebridge::sdk {

class Error : public std::runtime_error {
public:
    explicit Error(mbr_result result)
        : std::runtime_error(mbr_result_to_string(result)), result_(result) {}

    mbr_result result() const { return result_; }

private:
    mbr_result result_;
};

inline void Check(mbr_result result) {
    if (result != MBR_SUCCESS) {
        throw Error(result);
    }
}

class Runtime {
public:
    explicit Runtime(const mbr_runtime_config& config = {}) {
        Check(mbr_runtime_create(&config, &runtime_));
    }

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    Runtime(Runtime&& other) noexcept : runtime_(other.runtime_) {
        other.runtime_ = nullptr;
    }

    Runtime& operator=(Runtime&& other) noexcept {
        if (this != &other) {
            reset();
            runtime_ = other.runtime_;
            other.runtime_ = nullptr;
        }
        return *this;
    }

    ~Runtime() { reset(); }

    void connect(const std::string& endpoint = "local") {
        Check(mbr_runtime_connect(runtime_, endpoint.c_str()));
    }

    void disconnect() {
        mbr_runtime_disconnect(runtime_);
    }

    bool connected() const {
        return mbr_runtime_is_connected(runtime_) != 0;
    }

    mbr_frame_timing waitFrame(uint64_t timeoutNs = 0) {
        mbr_frame_timing timing{};
        Check(mbr_runtime_wait_frame(runtime_, timeoutNs, &timing));
        return timing;
    }

    mbr_headset_state headset() {
        mbr_headset_state state{};
        Check(mbr_runtime_get_headset_state(runtime_, &state));
        return state;
    }

    mbr_controller_state controller(uint32_t index) {
        mbr_controller_state state{};
        Check(mbr_runtime_get_controller_state(runtime_, index, &state));
        return state;
    }

    void submitSbsFrame(const mbr_sbs_frame_desc& frame) {
        Check(mbr_runtime_submit_sbs_frame(runtime_, &frame));
    }

    mbr_metrics metrics() const {
        mbr_metrics out{};
        Check(mbr_runtime_get_metrics(runtime_, &out));
        return out;
    }

    mbr_runtime* get() const { return runtime_; }

private:
    void reset() {
        if (runtime_) {
            mbr_runtime_destroy(runtime_);
            runtime_ = nullptr;
        }
    }

    mbr_runtime* runtime_ = nullptr;
};

} 
