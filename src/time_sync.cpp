#include "time_sync.hpp"

void TimeSync::resetPtsBaseUs(us_t pts) {
    std::lock_guard<std::mutex> lock(mtx);
    uniTimeStartUs = av_gettime();
    ptsBaseUs = pts;
}

 us_t TimeSync::getFrameTimeUs(us_t ptsUs, double speed) {
    std::lock_guard<std::mutex> lock(mtx);
    if (ptsBaseUs < 0) ptsBaseUs = ptsUs;
    return uniTimeStartUs + static_cast<int64_t>(std::llround((ptsUs - ptsBaseUs) / speed));
}