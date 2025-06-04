#include <iostream>
#include <cstring>

# include "video_player.hpp"

VideoPlayer::VideoPlayer(ST7735S& screen, uniframe::Orientation orientation) : screen(screen), orientation(orientation), running(false)
{
    // avformat_network_init();
}

VideoPlayer::~VideoPlayer()
{
    // stop();

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

    int indexVideoBest = -1;
    int indexAudioBest = -1;
    int indexSubtitleBest = -1;

    int scoreVideoBest = -1;
    int scoreAudioBest = -1;
    
    AVStream* stream = nullptr;
    AVCodecParameters* codecpar = nullptr;

    for (unsigned i = 0; i < formatCtx->nb_streams; i++) {
        stream = formatCtx->streams[i];
        codecpar = stream->codecpar;

        if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            int score = 0;
            if (stream->disposition & AV_DISPOSITION_DEFAULT) score += 100;
            score += codecpar->width * codecpar->height / 1000;
            if (score > scoreVideoBest) {
                scoreVideoBest = score;
                indexVideoBest = i;
            }
        } else if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            int score = 0;
            if (stream->disposition & AV_DISPOSITION_DEFAULT) score += 100;
            if (codecpar->sample_rate > 0) score += codecpar->sample_rate / 1000;

            if (score > scoreAudioBest) {
                scoreAudioBest = score;
                indexAudioBest = i;
            }
        }
        else if (codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            if (indexSubtitleBest == -1) {
                indexSubtitleBest = i;
            }
        }
    }

    if (indexVideoBest == -1) {
        std::cerr << "No valid video stream found (excluding attached pics)" << std::endl;
        return false;
    }

    streamIndexVideo = indexVideoBest;
    streamIndexAudio = indexAudioBest;
    streamIndexSubtitle = indexSubtitleBest;

    streamVideo = formatCtx->streams[streamIndexVideo];
    codecpar = streamVideo->codecpar;

    AVCodec* codec = nullptr;
    AVCodecContext* ctx = nullptr;

    auto try_open = [&](const char* name) -> bool {
        codec = avcodec_find_decoder_by_name(name);
        if (!codec) return false;
        ctx = avcodec_alloc_context3(codec);
        if (!ctx) return false;
        if (avcodec_parameters_to_context(ctx, codecpar) < 0) {
            avcodec_free_context(&ctx);
            return false;
        }
        if (avcodec_open2(ctx, codec, nullptr) < 0) {
            avcodec_free_context(&ctx);
            return false;
        }
        std::cout << "[Codec] Using hardware decoder: " << name << std::endl;
        return true;
    };

    bool hwSuccess = false;
    if (codecpar->codec_id == AV_CODEC_ID_H264) {
        hwSuccess = try_open("h264_v4l2m2m");
    } else if (codecpar->codec_id == AV_CODEC_ID_HEVC) {
        hwSuccess = try_open("hevc_v4l2m2m");
    }

    if (!hwSuccess) {
        codec = avcodec_find_decoder(codecpar->codec_id);
        if (!codec) {
            std::cerr << "No suitable decoder found" << std::endl;
            return false;
        }
        ctx = avcodec_alloc_context3(codec);
        if (!ctx) {
            std::cerr << "Failed to allocate codec context" << std::endl;
            return false;
        }
        if (avcodec_parameters_to_context(ctx, codecpar) < 0) {
            std::cerr << "Failed to copy codec parameters" << std::endl;
            avcodec_free_context(&ctx);
            return false;
        }
        if (avcodec_open2(ctx, codec, nullptr) < 0) {
            std::cerr << "Failed to open software decoder" << std::endl;
            avcodec_free_context(&ctx);
            return false;
        }
        std::cout << "[Codec] Using software decoder: " << codec->name << std::endl;
    }

    codecVideo = codec;
    codecCtxVideo = ctx;

    std::cout << "Selected video stream: #" << streamIndexVideo
              << " (" << codecCtxVideo->width << "x" << codecCtxVideo->height << ")" << std::endl;

    if (streamIndexAudio != -1) {
        std::cout << "Audio stream found: #" << streamIndexAudio << std::endl;
    }

    screen.rangeAdapt(codecCtxVideo->width, codecCtxVideo->height, orientation);
    
    // std::cout << "Loaded video: " << (formatCtx->url) << std::endl;
    // std::cout << "Duration: " << formatCtx->duration / double(AV_TIME_BASE) << " seconds" << std::endl;
    // std::cout << "Time base (video): " << streamVideo->time_base.num << "/" << streamVideo->time_base.den << std::endl;
    // std::cout << "Resolution: " << codecCtxVideo->width << " * " << codecCtxVideo->height << "    ";
    // std::cout << "FPS: " << static_cast<int>(av_q2d(streamVideo->avg_frame_rate)) << "    ";
    // std::cout << "Codec name: " << codecVideo->name << std::endl;

    return true;
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

        AVPacketPtr packet(av_packet_alloc());
        if (av_read_frame(formatCtx, packet.get()) < 0) {
            std::cout << "End" << std::endl;
            break;
        }

        // Demux for the video stream
        if (packet->stream_index != streamIndexVideo) continue;
        // std::cout << "[Decode] Packet pts: " << packet->pts << " dts: " << packet->dts << std::endl;
        std::unique_lock<std::mutex> lockPacket(mtxPacketVideo);
        cvPacketVideo.wait(lockPacket, [&]() {return queuePacketVideo.size() < maxQueueSizePacketVideo || !running;});
        if (!running) break;

        queuePacketVideo.push(std::move(packet));
        // lockPacket.unlock();
        cvPacketVideo.notify_one();
    }

    // Notify all threads to check the running status for quit.
    cvPacketVideo.notify_all();
}

void VideoPlayer::loopDecodeVideo()
{
    AVFramePtr frameRaw(av_frame_alloc());
    AVFramePtr frameDst(av_frame_alloc());
    SwsContext* swsCtx = nullptr;
    if (!frameRaw || !frameDst) {
        std::cerr << "Failed to allocate AVFrame" << std::endl;
        return;
    }

    int widthDst = screen.displayArea.displayWidth;
    int heightDst = screen.displayArea.displayHeight;
    AVPixelFormat pixelFormatDst = AV_PIX_FMT_RGB565BE;

    int ret = av_image_alloc(frameDst->data, frameDst->linesize, widthDst, heightDst, pixelFormatDst, 32);
    if (ret < 0) {
        std::cerr << "Failed to allocate destination image buffer" << std::endl;
        return;
    }

    frameDst->format = pixelFormatDst;
    frameDst->width = widthDst;
    frameDst->height = heightDst;

    swsCtx = sws_getContext(codecCtxVideo->width, codecCtxVideo->height, codecCtxVideo->pix_fmt, 
        widthDst, heightDst, pixelFormatDst, SWS_BICUBIC, nullptr, nullptr, nullptr);
    if (!swsCtx) {
        std::cerr << "Failed to initialize sws context" << std::endl;
        return;
    }

    while (running) {
        // Acquire packet from the packet queue.
        std::unique_lock<std::mutex> lockPacket(mtxPacketVideo);
        cvPacketVideo.wait(lockPacket, [&]() {return !queuePacketVideo.empty() || !running;});
        if (!running) break;

        AVPacketPtr packet = std::move(queuePacketVideo.front());
        queuePacketVideo.pop();
        lockPacket.unlock();
        cvPacketVideo.notify_one();

        ret = avcodec_send_packet(codecCtxVideo, packet.get());
        if (ret < 0) {
            std::cerr << "Failed to send packet to decoder" << std::endl;
            continue;
        }

        // Decode and scale.
        while (ret >= 0) {
            ret = avcodec_receive_frame(codecCtxVideo, frameRaw.get());
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) {
                std::cerr << "Failed to receive frame from decoder" << std::endl;
                break;
            }
            // std::cout << "[Decode] Got frame pts: " << frameRaw->pts << std::endl;
            sws_scale(swsCtx, frameRaw->data, frameRaw->linesize, 0, codecCtxVideo->height, frameDst->data, frameDst->linesize);
            int64_t pts = (frameRaw->pts != AV_NOPTS_VALUE) ? frameRaw->pts :
                (frameRaw->best_effort_timestamp != AV_NOPTS_VALUE) ? frameRaw->best_effort_timestamp :
                packet->pts;
            frameDst->pts = pts;

            std::unique_lock<std::mutex> lockRaw(mtxRawVideo);
            cvRawVideo.wait(lockRaw, [&]() {return queueRawVideo.size() < maxQueueSizeRawVideo || !running;});
            if (!running) break;
            queueRawVideo.push(std::move(frameDst));
            // lockRaw.unlock();
            cvRawVideo.notify_one();

            // Re-allocate the frameDst container for another push
            frameDst.reset(av_frame_alloc());
            av_image_alloc(frameDst->data, frameDst->linesize, widthDst, heightDst, pixelFormatDst, 32);
            frameDst->format = pixelFormatDst;
            frameDst->width = widthDst;
            frameDst->height = heightDst;
        }
    }
    
    sws_freeContext(swsCtx);
    cvRawVideo.notify_all();
}

void VideoPlayer::loopDisplayVideo()
{
    const int64_t timeStart = av_gettime();
    const AVRational time_base = streamVideo->time_base;
    const int widthDisplay = screen.displayArea.displayWidth;
    const int heightDisplay = screen.displayArea.displayHeight;
    const int bytesPerPixel = av_get_bits_per_pixel(av_pix_fmt_desc_get(AV_PIX_FMT_RGB565BE)) / 8;

    int64_t ptsStart = -1;

    // const int defaultFps = static_cast<int>(av_q2d(streamVideo->avg_frame_rate));
    // const int delayBase = (defaultFps > 0) ? static_cast<int>(1000.0 / defaultFps) : 40;

    while (running) {
        while (paused && running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        std::unique_lock<std::mutex> lockRaw(mtxRawVideo);
        cvRawVideo.wait(lockRaw, [&]() {return !queueRawVideo.empty() || !running;});
        if (!running) break;
        if (queueRawVideo.empty()) continue;

        AVFramePtr frame = std::move(queueRawVideo.front());
        queueRawVideo.pop();
        lockRaw.unlock();
        cvRawVideo.notify_one();

        // Prepare the frame buffer
        std::cout << "[Display] Frame displayed: pts=" << frame->pts << std::endl;
        std::vector<uint8_t> buffer(widthDisplay * heightDisplay * bytesPerPixel);
        if (frame->linesize[0] == widthDisplay * bytesPerPixel) {
            std::memcpy(buffer.data(), frame->data[0], buffer.size());
        } else {
            for (int y = 0; y < heightDisplay; ++y) {
                std::memcpy(buffer.data() + y * heightDisplay * bytesPerPixel,
                            frame->data[0] + y * frame->linesize[0],
                            widthDisplay * bytesPerPixel);
            }
        }

        if (frame->pts == AV_NOPTS_VALUE) {
            std::cerr << "[Display] Frame has no PTS, skipping." << std::endl;
            //continue;
        }

        // Get pts in μs of the frame
        int64_t ptsFrame = av_rescale_q(frame->pts, time_base, AVRational{1, 1000000});
        
        if (ptsStart < 0) ptsStart = ptsFrame;

        int64_t timeTarget = timeStart + static_cast<int64_t>((ptsFrame - ptsStart) / speedFactor.load());
        int64_t timeNow = av_gettime();

        if (timeTarget > timeNow) {
            std::this_thread::sleep_for(std::chrono::microseconds(timeTarget - timeNow));
        }

        // Display frame
        screen.startWrite();
        screen.writeData(buffer.data(), buffer.size());
    }
    std::cout << "[Display] thread exit" << std::endl;
}

void VideoPlayer::play()
{
    if (running) return;
    running = true;
    paused = false;

    std::cout << "[Play] Starting video playback threads..." << std::endl;

    threadDemux = std::thread(&VideoPlayer::loopDemux, this);
    threadDecodeVideo = std::thread(&VideoPlayer::loopDecodeVideo, this);
    threadDisplay = std::thread(&VideoPlayer::loopDisplayVideo, this);
}

void VideoPlayer::stop()
{
    if (!running) return;
    running = false;

    cvPacketVideo.notify_all();
    cvRawVideo.notify_all();

    if (threadDemux.joinable()) threadDemux.join();
    if (threadDecodeVideo.joinable()) threadDecodeVideo.join();
    if (threadDisplay.joinable()) threadDisplay.join();

    while (!queuePacketVideo.empty()) queuePacketVideo.pop();
    while (!queueRawVideo.empty()) queueRawVideo.pop();
}

void VideoPlayer::pauseResume() {
    paused = !paused;
}

