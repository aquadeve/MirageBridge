#include <stdio.h>
#include <time.h>

#include "mirage_runtime.h"

static void sleep_ms(long ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, 0);
}

int main(void) {
    mbr_runtime_config cfg = {0};
    cfg.application_name = "mbr-pose-viewer";

    mbr_runtime* runtime = 0;
    mbr_result result = mbr_runtime_create(&cfg, &runtime);
    if (result != MBR_SUCCESS) {
        printf("create failed: %s\n", mbr_result_to_string(result));
        return 1;
    }

    result = mbr_runtime_connect(runtime, "local");
    if (result != MBR_SUCCESS) {
        printf("connect failed: %s\n", mbr_result_to_string(result));
        mbr_runtime_destroy(runtime);
        return 1;
    }

    for (;;) {
        mbr_frame_timing timing;
        result = mbr_runtime_wait_frame(runtime, 1000000000ull, &timing);
        if (result != MBR_SUCCESS) {
            printf("waitFrame: %s\n", mbr_result_to_string(result));
            sleep_ms(100);
            continue;
        }

        mbr_headset_state headset;
        result = mbr_runtime_get_headset_state(runtime, &headset);
        if (result == MBR_SUCCESS) {
            printf("frame=%llu pos=(%+.3f,%+.3f,%+.3f) quat=(%+.3f,%+.3f,%+.3f,%+.3f) period=%lluns\n",
                   (unsigned long long)headset.frame_id,
                   headset.pose.position.x,
                   headset.pose.position.y,
                   headset.pose.position.z,
                   headset.pose.rotation.x,
                   headset.pose.rotation.y,
                   headset.pose.rotation.z,
                   headset.pose.rotation.w,
                   (unsigned long long)timing.display_period_ns);
        }
        sleep_ms(16);
    }
}
