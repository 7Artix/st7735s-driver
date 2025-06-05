#pragma once

#include <mutex>
#include <atomic>
#include <cstdint>

extern "C" {
#include <libavutil/time.h>
#include <libavutil/avutil.h>
}

typedef int64_t us_t;
typedef int64_t tb_t;

class TimeSync {
public:
    void resetPtsBaseUs(us_t ptsUs);
    us_t getFrameTimeUs(us_t ptsUs, double speed);

private:
    std::mutex mtx;
    us_t uniTimeStartUs = 0;
    us_t ptsBaseUs = -1;
};