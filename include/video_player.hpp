#pragma once

#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <string>
#include <chrono>

#include "uni_frame.hpp"
#include "st7735s.hpp"
#include "time_sync.hpp"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libavutil/time.h>
}

class VideoPlayer {
public:
    explicit VideoPlayer(ST7735S& screen, uniframe::Orientation orientation);
    ~VideoPlayer();

    bool load(const std::string& path);
    void play();
    void wait();
    void stop();
    void pauseResume();
    void seekForward(us_t us);
    void seekBackward(us_t us);
    double setSpeed(double dFactor);
private:
    ST7735S& screen;

    uniframe::Orientation orientation;

    us_t seekUsForward = 5e06;
    us_t seekUsBackward = 5e06;

    const size_t maxQueueSizePacketVideo = 10;
    const size_t maxQueueSizeRawVideo = 10;

    us_t durationUs = 0;

    // Smart pointer for allocating and memory management.
    struct AVFrameDeleter {
        void operator()(AVFrame* f) const { av_frame_free(&f); }
    };
    using AVFramePtr = std::unique_ptr<AVFrame, AVFrameDeleter>;

    struct AVPacketDeleter {
        void operator()(AVPacket* p) const { av_packet_free(&p); }
    };
    using AVPacketPtr = std::unique_ptr<AVPacket, AVPacketDeleter>;

    // Time sync management
    TimeSync timeSync;

    std::queue<AVPacketPtr> queuePacketVideo;
    std::queue<AVFramePtr> queueRawVideo;

    std::thread threadDemux;
    std::thread threadDecodeVideo;
    // std::thread threadDecodeAudio;
    std::thread threadDisplay;
    std::thread threadControl;

    std::mutex mtxPacketVideo;
    // std::mutex mtxPacketAudio;
    std::mutex mtxRawVideo;
    // std::mutex mtxRawAudio;

    std::condition_variable cvPacketVideo;
    // std::condition_variable cvPacketAudio;
    std::condition_variable cvRawVideo;
    // std::condition_variable cvRawAudio;


    // // deprecated use smart pointer instead.
    // std::queue<AVPacket*> queuePacketVideo;
    // std::queue<AVFrame*> queueRawVideo;
    
    // // deprecated use AVFrame instead
    // std::queue<std::unique_ptr<uniframe::ImageRGB565>> queueRawVideo;
    // uniframe::FramePool framePool{maxQueueSizeRawVideo};

    std::atomic<bool> running;
    std::atomic<bool> flushing{false};
    std::atomic<bool> seekRequest{false};
    std::atomic<us_t> seekTargetUs{0};
    std::atomic<double> speedFactor{1.0};
    std::atomic<bool> paused{false};
    std::atomic<bool> resetTimeRequest{false};
    std::atomic<us_t> currentPtsUs{0};

    AVFormatContext* formatCtx = nullptr;

    AVCodecContext* codecCtxVideo = nullptr;
    AVCodecContext* codecCtxAudio = nullptr;

    AVCodec* codecVideo = nullptr;
    AVCodec* codecAudio = nullptr;

    AVStream* streamVideo = nullptr;
    AVStream* streamAudio = nullptr;

    int streamIndexVideo = -1;
    int streamIndexAudio = -1;
    int streamIndexSubtitle = -1;

    void loopDemux();
    void loopDecodeVideo();
    void loopDisplayVideo();
    void loopControl();
};