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

}