#include <iostream>
#include <cstring>

# include "video_player.hpp"

VideoPlayer::VideoPlayer(ST7735S& screen) : screen(screen), running(false)
{
    // avformat_network_init();
}

VideoPlayer::~VideoPlayer()
{
    // stop();

    while (!queuePacketVideo.empty()) {
        AVPacket* pkt = queuePacketVideo.front();
        queuePacketVideo.pop();
        av_packet_free(&pkt);
    }

    if (codecCtxVideo) {
        avcodec_free_context(&codecCtxVideo);
        codecCtxVideo = nullptr;
    }

    if (formatCtx) {
        avformat_close_input(&formatCtx);
        formatCtx = nullptr;
    }

    // avformat_network_deinit();
}

bool VideoPlayer::load(const std::string& path)
{
    if (avformat_open_input(&formatCtx, path.c_str(), nullptr, nullptr) != 0) {
        std::cerr << "Failed to open video file: " << path << std::endl;
        return false;
    }

    if (avformat_find_stream_info(formatCtx, nullptr) < 0) {
        std::cerr << "Failed to find stream info" << std::endl;
        return false;
    }

    for (unsigned i = 0; i < formatCtx->nb_streams; i++) {
        if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            streamIndexVideo = i;
        } else if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            streamIndexAudio = i;
        } else if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            streamIndexSubtitle = i;
        }
    }

    if (streamIndexVideo == -1) {
        std::cerr << "No video stream found" << std::endl;
        return false;
    }

    streamVideo = formatCtx->streams[streamIndexVideo];
    codecVideo = avcodec_find_decoder(streamVideo->codecpar->codec_id);
    if (!codecVideo) {
        std::cerr << "Unsupported video codec" << std::endl;
        return false;
    }

    codecCtxVideo = avcodec_alloc_context3(codecVideo);
    if (avcodec_parameters_to_context(codecCtxVideo, streamVideo->codecpar) < 0) {
        std::cerr << "Failed to copy video codec parameters" << std::endl;
        return false;
    }

    if (avcodec_open2(codecCtxVideo, codecVideo, nullptr) < 0) {
        std::cerr << "Failed to open video codec" << std::endl;
        return false;
    }

    std::cout << "Loaded video: " << (formatCtx->url) << std::endl;
    std::cout << "Duration: " << formatCtx->duration / double(AV_TIME_BASE) << " seconds" << std::endl;
    std::cout << "Time base (video): " << streamVideo->time_base.num << "/" << streamVideo->time_base.den << std::endl;
    std::cout << "Resolution: " << codecCtxVideo->width << " * " << codecCtxVideo->height << "    ";
    std::cout << "FPS: " << static_cast<int>(av_q2d(streamVideo->avg_frame_rate)) << "    ";
    std::cout << "Codec name: " << codecVideo->name << std::endl;

    return true;
}

void VideoPlayer::loopDecodeVideo()
{
    AVFrame* frameRaw = av_frame_alloc();
    AVFrame* frameDst = av_frame_alloc();
    SwsContext* swsCtx = nullptr;
    if (!frameRaw || !frameDst) {
        std::cerr << "Failed to allocate AVFrame" << std::endl;
        return;
    }

    int widthDst = screen.screenWidth;
    int heightDst = screen.screenHeight;
    AVPixelFormat pixelFormatDst = AV_PIX_FMT_RGB565BE;

    int ret = av_image_alloc(frameDst->data, frameDst->linesize, widthDst, heightDst, pixelFormatDst, 32);
    if (ret < 0) {
        std::cerr << "Failed to allocate destination image buffer" << std::endl;
        return;
    }

    frameDst->format = AV_PIX_FMT_RGB565BE;
    frameDst->width = widthDst;
    frameDst->height = heightDst;

    int width = codecCtxVideo->width;
    int height = codecCtxVideo->height;

    swsCtx = sws_getContext(width, height, codecCtxVideo->pix_fmt, widthDst, heightDst, pixelFormatDst, SWS_BICUBIC, nullptr, nullptr, nullptr);
    if (!swsCtx) {
        std::cerr << "Failed to initialize sws context" << std::endl;
        return;
    }

    while (running) {
        // Acquire packet from the packet queue.
        std::unique_lock<std::mutex> lockPacket(mtxPacketVideo);
        cvPacketVideo.wait(lockPacket, [&]() {return !queuePacketVideo.empty() || !running;});
        if (!running) break;
        AVPacket* packet = queuePacketVideo.front();
        queuePacketVideo.pop();
        lockPacket.unlock();
        cvPacketVideo.notify_one();

        ret = avcodec_send_packet(codecCtxVideo, packet);
        av_packet_free(&packet);
        if (ret < 0) {
            std::cerr << "Failed to send packet to decoder" << std::endl;
            continue;
        }

        // Decode and scale.
        while (ret >= 0) {
            ret = avcodec_receive_frame(codecCtxVideo, frameRaw);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) {
                std::cerr << "Failed to receive frame from decoder" << std::endl;
                break;
            }

            sws_scale(swsCtx, frameRaw->data, frameRaw->linesize, 0, height, frameDst->data, frameDst->linesize);
            std::unique_lock<std::mutex> lockRaw(mtxRawVideo);
            cvRawVideo.wait(lockRaw, [&]() {return queueRawVideo.size() < maxQueueSizeRawVideo || running;});
            if (!running) break;
            queueRawVideo.push(frameDst);
            // lockRaw.unlock();
            cvRawVideo.notify_one();
        }

        av_frame_free(&frameRaw);
        av_frame_free(&frameDst);
        sws_freeContext(swsCtx);
    }

    cvRawVideo.notify_all();
}

void VideoPlayer::loopDemux()
{
    while (running) {
        if (seekRequest) {
            int64_t seekTarget = static_cast<int64_t>(seekTargetSeconds) * AV_TIME_BASE;
            if (av_seek_frame(formatCtx, streamIndexVideo, seekTarget, AVSEEK_FLAG_BACKWARD) < 0) {
                std::cerr << "Seek failed" << std::endl;
            } else {
                avcodec_flush_buffers(codecCtxVideo);
            }
            seekRequest = false;
            continue;
        }
        AVPacket* packet = av_packet_alloc();
        if (av_read_frame(formatCtx, packet) < 0) {
            std::cout << "End" << std::endl;
            av_packet_free(&packet);
            break;
        }

        if (packet->stream_index != streamIndexVideo) {
            av_packet_free(&packet);
            continue;
        }

        std::unique_lock<std::mutex> lockPacket(mtxPacketVideo);
        cvPacketVideo.wait(lockPacket, [&]() {queuePacketVideo.size() < maxQueueSizePacketVideo || !running;});
        if (!running) break;
        queuePacketVideo.push(packet);
        // lockPacket.unlock();
        cvPacketVideo.notify_one();
    }

    // Notify all threads to check the running status for quit.
    cvPacketVideo.notify_all();
}