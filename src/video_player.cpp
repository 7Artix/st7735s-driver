#include <iostream>
#include <cstring>
#include <termios.h>
#include <unistd.h>
#include <algorithm> 

# include "video_player.hpp"

// #define DEBUG_OUTPUT

// Swith the terminal into the raw input mode (input the command without "return");
namespace {
    struct TerminalRawMode {
        termios orig;
        TerminalRawMode() {
            tcgetattr(STDIN_FILENO, &orig);
            termios raw = orig;
            raw.c_lflag &= ~(ICANON | ECHO);
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
        }
        ~TerminalRawMode() {
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);
        }
    };
}

VideoPlayer::VideoPlayer(ST7735S& screen, uniframe::Orientation orientation)
    : screen(screen), orientation(orientation), timeSync(), running(false)
{
    // avformat_network_init();
    static TerminalRawMode terminalModeGuard;
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

    durationUs = formatCtx->duration;
    
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
            flushing.store(true);

            // Awake the other threads to response the request.
            // cvPacketVideo.notify_all();
            // cvRawVideo.notify_all();

            std::this_thread::sleep_for(std::chrono::milliseconds(10));

            {
                std::lock_guard<std::mutex> lockPacket(mtxPacketVideo);
                while (!queuePacketVideo.empty()) queuePacketVideo.pop();
            }
            {
                std::lock_guard<std::mutex> lockRaw(mtxRawVideo);
                while (!queueRawVideo.empty()) queueRawVideo.pop();
            }

            tb_t timestempTarget = av_rescale_q(seekTargetUs, AVRational{1, 1000000}, streamVideo->time_base);
            if (av_seek_frame(formatCtx, streamIndexVideo, timestempTarget, AVSEEK_FLAG_BACKWARD) < 0) {
                std::cerr << "Seek failed" << std::endl;
            } else {
                avcodec_flush_buffers(codecCtxVideo);
            }

            resetTimeRequest.store(true);
            seekRequest.store(false);
            flushing.store(false);

            cvPacketVideo.notify_all();
            cvRawVideo.notify_all();
            std::cout << "Seek request handled" << std::endl;
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
        cvPacketVideo.wait(lockPacket, [&]() { return (!running) || (queuePacketVideo.size() < maxQueueSizePacketVideo);});
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

    std::cout << "Decode pre handled" << std::endl;

    while (running) {
        // Acquire packet from the packet queue.
        std::unique_lock<std::mutex> lockPacket(mtxPacketVideo);
        cvPacketVideo.wait(lockPacket, [&]() { return (!running) || (!flushing && !queuePacketVideo.empty());});
        if (!running) break;
        // if (flushing) continue;
        // if (queuePacketVideo.empty()) continue;

        std::cout << "Enter decode, flushing: " << flushing << std::endl;
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
            cvRawVideo.wait(lockRaw, [&]() { return (!running) || (!flushing && queueRawVideo.size() < maxQueueSizeRawVideo);});
            if (!running) break;
            // if (flushing) continue;
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
    const AVRational time_base = streamVideo->time_base;
    const int widthDisplay = screen.displayArea.displayWidth;
    const int heightDisplay = screen.displayArea.displayHeight;
    const int bytesPerPixel = av_get_bits_per_pixel(av_pix_fmt_desc_get(AV_PIX_FMT_RGB565BE)) / 8;
    resetTimeRequest.store(true);

    std::cout << "Display pre handled" << std::endl;

    while (running) {
        while (paused) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        std::unique_lock<std::mutex> lockRaw(mtxRawVideo);
        cvRawVideo.wait(lockRaw, [&]() { return (!running) || (!flushing && !queueRawVideo.empty());});
        if (!running) break;
        // if (flushing) continue;
        // if (queueRawVideo.empty()) continue;

        std::cout << "Enter display, flushing: " << flushing << std::endl;

        AVFramePtr frame = std::move(queueRawVideo.front());
        queueRawVideo.pop();
        lockRaw.unlock();
        cvRawVideo.notify_one();

        if (frame->pts == AV_NOPTS_VALUE) {
            std::cerr << "[Display] Frame has no PTS, skipping." << std::endl;
            continue;
        }

        // Get pts in μs of the frame
        us_t ptsFrameUs = av_rescale_q(frame->pts, time_base, AVRational{1, 1000000});
        this->currentPtsUs = ptsFrameUs;

        // If need request time
        if (resetTimeRequest.exchange(false)) {
            timeSync.resetPtsBaseUs(ptsFrameUs);
        }
        us_t timeTargetUs = timeSync.getFrameTimeUs(ptsFrameUs, speedFactor.load());
        us_t timeNowUs = av_gettime();

        if (timeTargetUs > timeNowUs) {
            std::this_thread::sleep_for(std::chrono::microseconds(timeTargetUs - timeNowUs));
        }

        // Prepare the frame buffer
#ifdef DEBUG_OUTPUT
        std::cout << "[Display] Frame displayed: pts=" << frame->pts << std::endl;
#endif
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

        // Display frame
        screen.startWrite();
        screen.writeData(buffer.data(), buffer.size());
    }
    std::cout << "[Display] thread exit" << std::endl;
}

void VideoPlayer::loopControl()
{
    while (running) {
        int cmd = getchar();
        if (cmd == EOF) continue;

        if (cmd == '\x1b') {
            char next1 = getchar();
            char next2 = getchar();
            if (next1 == '[') {
                switch (next2) {
                    case 'A': std::cout << "↑Up\n"; break;
                    case 'B': std::cout << "↓Down\n"; break; 
                    case 'C': std::cout << "→Right\n"; seekForward(seekUsForward); break;
                    case 'D': std::cout << "←Left\n"; seekBackward(seekUsBackward); break;
                    default: break;
                }
            }
        } else {
            switch (cmd) {
                case ' ':
                    pauseResume();
                    break;
                case '[': {
                    std::cout << "[Control] Speed: " << setSpeed(-0.1) << "*" << std::endl;
                    break;
                }
                case ']': {
                    std::cout << "[Control] Speed: " << setSpeed(0.1) << "*" << std::endl;
                    break;
                }
                default:
                    break;
            }
        }
    }
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
    threadControl = std::thread(&VideoPlayer::loopControl, this);
}

void VideoPlayer::wait()
{
    if (threadDemux.joinable()) threadDemux.join();
    if (threadDecodeVideo.joinable()) threadDecodeVideo.join();
    if (threadDisplay.joinable()) threadDisplay.join();
    if (threadControl.joinable()) threadControl.join();
}

void VideoPlayer::stop()
{
    if (!running) return;
    running = false;
    paused = false;

    cvPacketVideo.notify_all();
    cvRawVideo.notify_all();

    if (threadDemux.joinable()) threadDemux.join();
    if (threadDecodeVideo.joinable()) threadDecodeVideo.join();
    if (threadDisplay.joinable()) threadDisplay.join();
    if (threadControl.joinable()) threadControl.join();

    while (!queuePacketVideo.empty()) queuePacketVideo.pop();
    while (!queueRawVideo.empty()) queueRawVideo.pop();
}

void VideoPlayer::pauseResume() {
    paused = !paused;
    resetTimeRequest.store(true);
}

void VideoPlayer::seekForward(us_t us)
{
    resetTimeRequest.store(true);
    std::cout << "Enter seek forward" << std::endl;
    us_t current = currentPtsUs.load();
    us_t next = std::clamp(static_cast<us_t>(current + us), static_cast<us_t>(0), durationUs);
    seekTargetUs.store(next);
    seekRequest.store(true);
}

void VideoPlayer::seekBackward(us_t us)
{
    resetTimeRequest.store(true);
    std::cout << "Enter seek backward" << std::endl;
    us_t current = currentPtsUs.load();
    us_t next = std::clamp(static_cast<us_t>(current - us), static_cast<us_t>(0), durationUs);
    seekTargetUs.store(next);
    seekRequest.store(true);
}

double VideoPlayer::setSpeed(double dFactor)
{
    resetTimeRequest.store(true);
    double speedTarget = (speedFactor + dFactor) <= 0.1 ? 0.1 : (speedFactor + dFactor);
    speedFactor.store(speedTarget);
    return speedTarget;
}