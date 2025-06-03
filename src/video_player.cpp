#include <iostream>

# include "video_player.hpp"

VideoPlayer::VideoPlayer(ST7735S& screen) : screen(screen), running(false)
{
    // avformat_network_init();
}

VideoPlayer::~VideoPlayer()
{
    // stop();

    if (codecVideoCtx) {
        avcodec_free_context(&codecVideoCtx);
        codecVideoCtx = nullptr;
    }

    if (formatCtx) {
        avformat_close_input(&formatCtx);
        formatCtx = nullptr;
    }

    if (packet) av_packet_free(&packet);
    if (frame) av_frame_free(&frame);

    // avformat_network_deinit();
}

bool VideoPlayer::load(const std::string& path) {
    packet = av_packet_alloc();
    frame = av_frame_alloc();

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

    codecVideoCtx = avcodec_alloc_context3(codecVideo);
    if (avcodec_parameters_to_context(codecVideoCtx, streamVideo->codecpar) < 0) {
        std::cerr << "Failed to copy video codec parameters" << std::endl;
        return false;
    }

    if (avcodec_open2(codecVideoCtx, codecVideo, nullptr) < 0) {
        std::cerr << "Failed to open video codec" << std::endl;
        return false;
    }

    std::cout << "Loaded video: " << (formatCtx->url) << std::endl;
    std::cout << "Duration: " << formatCtx->duration / double(AV_TIME_BASE) << " seconds" << std::endl;
    std::cout << "Time base (video): " << streamVideo->time_base.num << "/" << streamVideo->time_base.den << std::endl;
    std::cout << "Resolution: " << codecVideoCtx->width << " * " << codecVideoCtx->height << "    ";
    std::cout << "FPS: " << static_cast<int>(av_q2d(streamVideo->avg_frame_rate)) << "    ";
    std::cout << "Codec name: " << codecVideo->name << std::endl;

    return true;
}

void VideoPlayer::decodeLoop()
{
    const size_t maxQueueSize = 5;

    while (running) {
        // Handle the seek request.
        if (seekRequest) {
            int64_t seekTarget = static_cast<int64_t>(seekTargetSeconds) * AV_TIME_BASE;
            if (av_seek_frame(formatCtx, streamIndexVideo, seekTarget, AVSEEK_FLAG_BACKWARD) < 0) {
                std::cerr << "Seek failed" << std::endl;
            } else {
                avcodec_flush_buffers(codecVideoCtx);
            }
            seekRequest = false;
            continue;
        }

        std::unique_lock<std::mutex> lockDecode(mtxDecodeQueue);
        cvDecode.wait(lockDecode, [&]() {return queueDecode.size() < maxQueueSize || !running;});

        if (av_read_frame(formatCtx, packet) < 0) {
            std::cerr << "The End or error" << std::endl;
            break;
        }

        // Skip the non-video stream.
        if (packet->stream_index != streamIndexVideo) {
            av_packet_unref(packet);
            continue;
        }

        
    }
}