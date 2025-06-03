#pragma once

#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <string>

#include "uni_frame.hpp"
#include "st7735s.hpp"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

class VideoPlayer {
public:
    explicit VideoPlayer(ST7735S& screen);
    ~VideoPlayer();

    bool load(const std::string& path);
    void play();
    void stop();
    void pauseResume();
    void forward(int second);
    void backward(int second);
    void speed(double factor);

private:
    ST7735S& screen;

    std::thread threadDecode;
    std::thread threadConvert;
    std::thread threadDisplay;
    std::thread threadControl;
    std::thread threadAudio;

    std::mutex mtxDecodeQueue;
    std::mutex mtxConvertQueue;

    std::condition_variable cvDecode;
    std::condition_variable cvConvert;
    std::condition_variable cvDisplay;

    std::queue<uniframe::ImageRGB24> queueDecode;
    std::queue<uniframe::ImageRGB565> queueConvert;

    std::atomic<bool> running;
    std::atomic<bool> seekRequest{false};
    std::atomic<int> seekTargetSeconds{0};
    std::atomic<double> speedFactor{1.0};
    std::atomic<bool> paused{false};

    AVFormatContext* formatCtx = nullptr;

    AVCodecContext* codecVideoCtx = nullptr;
    AVCodec* codecVideo = nullptr;
    AVStream* streamVideo = nullptr;
    
    AVCodecContext* codecAudioCtx = nullptr;
    AVCodec* codecAudio = nullptr;
    AVStream* streamAudio = nullptr;

    int streamIndexVideo = -1;
    int streamIndexAudio = -1;
    int streamIndexSubtitle = -1;

    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;

    void decodeLoop();
    void convertLoop();
    void displayLoop();
};