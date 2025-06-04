#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace uniframe {

struct ImageRGB565 {
    int width;
    int height;
    std::vector<uint8_t> data;
};

struct ImageRGB24 {
    int width;
    int height;
    std::vector<uint8_t> data;
};

enum class ImageType {
    PNG,
    JPG
};

class FramePool {
public:
    FramePool(size_t maxsize);
    ~FramePool();

    std::unique_ptr<ImageRGB565> acquire();
    void release(std::unique_ptr<ImageRGB565> frameRaw);

private:
    std::queue<std::unique_ptr<ImageRGB565>> pool;
    std::mutex mtx;
};

}