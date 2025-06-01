#include "image_handler.hpp"
#include <fstream>
#include <cstring>
#include <stdexcept>
#include <iostream>

#include <turbojpeg.h>
#include <png.h>
#include <libyuv.h>

namespace imghandler {

ImageType formatProbe(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open image file");
    }

    uint8_t header[8] = {};
    file.read(reinterpret_cast<char*>(header), sizeof(header));

    if (std::memcmp(header, "\xFF\xD8", 2) == 0) {
        return ImageType::JPG;
    }
    if (png_sig_cmp(header, 0, 8) == 0) {
        return ImageType::PNG;
    }
    throw std::runtime_error("Unsupported image format");
}

bool decodeJpegToRGB24(const std::string& filename, ImageRGB24& image)
{
    std::ifstream file(filename, std::ios::binary);
    if (!file) return false;
    std::vector<uint8_t> jpegBuf((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    tjhandle handle = tjInitDecompress();
    if (!handle) throw std::runtime_error("Decompressor init failed");
    if (tjDecompressHeader(handle, jpegBuf.data(), jpegBuf.size(), &image.width, &image.height)) {
        tjDestroy(handle);
        return false;
    }
    image.data.resize(image.width * image.height * 3);
    if (tjDecompress2(handle, jpegBuf.data(), jpegBuf.size(), image.data.data(), image.width, 0, image.height, TJPF_RGB, TJFLAG_FASTDCT)) {
        tjDestroy(handle);
        return false;
    }
    tjDestroy(handle);
    return true;
}

bool scaleImage(const ImageRGB24& src, ImageRGB24& dst, int targetWidth, int targetHeight)
{
    dst.width = targetWidth;
    dst.height = targetHeight;
    dst.data.resize(targetWidth * targetHeight * 3);

    std::vector<uint8_t> srcARGB(src.width * src.height * 4);
    std::vector<uint8_t> dstARGB(dst.width * dst.height * 4);

    if (libyuv::RAWToARGB(src.data.data(),src.width * 3, srcARGB.data(), src.width * 4, src.width, src.height) != 0) return false;
    if (libyuv::ARGBScale(srcARGB.data(), src.width * 4, src.width, src.height, dstARGB.data(), dst.width * 4, dst.width, dst.height, libyuv::kFilterBox) != 0) return false;
    if (libyuv::ARGBToRAW(dstARGB.data(), dst.width * 4, dst.data.data(), dst.width * 3, dst.width, dst.height) != 0) return false;

    return true;
}

bool convertToRGB565(const ImageRGB24& src, ImageRGB565& dst)
{
    dst.width = src.width;
    dst.height = src.height;
    dst.data.resize(src.width * src.height * 2);

    std::vector<uint8_t> ARGB(src.width * src.height * 4);

    if (libyuv::RAWToARGB(src.data.data(), src.width * 3, ARGB.data(), src.width * 4, src.width, src.height) != 0) return false;
    if (libyuv::ARGBToRGB565(ARGB.data(), src.width * 4, dst.data.data(), dst.width * 2, dst.width, dst.height) != 0) return false;
    for (size_t i = 0; i < dst.data.size(); i += 2) {
        std::swap(dst.data[i], dst.data[i + 1]);
    }
    
    std::vector<uint8_t> sub(dst.data.begin() + 0, dst.data.begin() + 10);
    for (uint8_t b : sub) {
        std::cout << "0x" << std::hex << static_cast<int>(b) << " ";
    }
    std::cout << std::endl;
    
    return true;
}

}
