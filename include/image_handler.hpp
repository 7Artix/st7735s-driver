#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace imghandler {

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

ImageType formatProbe(const std::string& path);

bool decodeJpegToRGB24(const std::string& filename, ImageRGB24& image);
bool decodePngToRGB24(const std::string& filename, ImageRGB24& image);

bool convertToRGB565(const ImageRGB24& src, ImageRGB565& dst);
bool scaleImage(const ImageRGB24& src, ImageRGB24& dst, int targetWidth, int targetHeight);
}