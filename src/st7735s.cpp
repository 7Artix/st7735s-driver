#include <st7735s.hpp>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <stdexcept>
#include <iostream>
#include <chrono>
#include <thread>
#include <cmath>

ST7735S::ST7735S(const std::string& spi_dev, 
    const std::string& gpio_chip_name_rst, 
    const uint8_t gpio_offset_rst, 
    const std::string& gpio_chip_name_dc,  
    const uint8_t gpio_offset_dc)
{
    spi_fd = open(spi_dev.c_str(), O_RDWR);
    if (spi_fd < 0) {
        throw std::runtime_error("Failed to open SPI device" + spi_dev);
    }

    // SPI configuration
    uint32_t mode = SPI_MODE_0;
    uint8_t bits = 8;
    if(ioctl(spi_fd, SPI_IOC_WR_MODE, &mode) < 0) {
        throw std::runtime_error("Failed to set SPI mode");
    }
    if(ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        throw std::runtime_error("Failed to set SPI speed");
    }
    if(ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) {
        throw std::runtime_error("Failed to set SPI bits per word");
    }

    gpiod::chip chip_rst(gpio_chip_name_rst);
    gpiod::chip chip_dc(gpio_chip_name_dc);
    gpio_line_rst = chip_rst.get_line(gpio_offset_rst);
    gpio_line_dc = chip_dc.get_line(gpio_offset_dc);

    if (gpio_line_rst.is_used() || gpio_line_dc.is_used()) {
        throw std::runtime_error("GPIO pins are in use");
    }

    gpio_line_rst.request({"st7735s_rst", gpiod::line_request::DIRECTION_OUTPUT, 0}, 1);
    gpio_line_dc.request({"st7735s_dc", gpiod::line_request::DIRECTION_OUTPUT, 0}, 1);
}

ST7735S::~ST7735S()
{
    gpio_line_rst.release();
    gpio_line_dc.release();
    close(spi_fd);
}

void ST7735S::spiTransfer(bool isData, const uint8_t* data, size_t len)
{
    gpio_line_dc.set_value(isData ? 1 : 0);
    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)data,
        .len = static_cast<unsigned int>(len),
        .speed_hz = speed,
        .delay_usecs = 0,
        .bits_per_word = 8
    };
    if (ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr) < 0) {
        throw std::runtime_error("SPI transfer failed");
    }
}

void ST7735S::writeCmd(uint8_t cmd)
{
    spiTransfer(false, &cmd, 1);
}

void ST7735S::writeData(const uint8_t* data, size_t len)
{
    size_t offset = 0;
    while (offset < len) {
        size_t chunkSize = std::min(maxSPIChunkSize, len - offset);
        spiTransfer(true, data + offset, chunkSize);
        offset += chunkSize;
    }
}

void ST7735S::writeData(uint8_t singleByte)
{
    writeData(&singleByte, 1);
}

void ST7735S::startWrite()
{
    writeCmd(0x2C);
}

void ST7735S::delay_ms(uint64_t ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void ST7735S::reset()
{
    // Set RST to "0" for reset.
    gpio_line_rst.set_value(0);
    delay_ms(50);
    gpio_line_rst.set_value(1);
    delay_ms(50);
}

void ST7735S::init()
{
    // Panel Resolution Select:
    // GM2=0 GM1=1 GM0=1
    // 128RGB * 160 (S7~390 and G2~161 output)
    // S:LCD Source Driver[396:1]
    // G:LCD Gate   Driver[162:1]
    reset();
    sleepMode(false); // Awake
    // Pixel Format
    writeCmd(0x3A);
    writeData(0x55); // RGB565

    // Gamma select
    writeCmd(0x26);
    writeData(0x03);
    // gammaCorrect();

    // FPS formula: fps = 200kHz/(line+VPA[5:0])(DIVA[4:0]+4)
    // when GM = 011(128*160), line = 160
    // FPS select (normal mode, full colors)
    writeCmd(0XB1); // normal mode
    writeData(0x06);
    writeData(0x0A); // fps: 117
    writeCmd(0XB2); // idle mode
    writeData(0x06);
    writeData(0x0A); // fps: 117
    writeCmd(0XB3); // partial mode
    writeData(0x06);
    writeData(0x0A); // fps: 117

    // Display Inversion Control (refresh by line or frame)
    writeCmd(0xB4);
    writeData(0x02);
    // Power control: GVDD and voltage
    writeCmd(0xC0);
    writeData(0x0A);
    writeData(0x02);
    // Power control: AVDD, VCL, VGH and VGL supply power level
    writeCmd(0xC1);
    writeData(0x02);
    // Power control: Set VCOMH, VCOML Voltage
    writeCmd(0xC5);
    writeData(0x4F);
    writeData(0x5A);
    // VCOM Offset Control
    writeCmd(0xC7);
    writeData(0x40);

    colorInversion(false);
    colorOrderRGB(true);

    rangeReset();
    idleMode(false);

    // Source Driver Direction Control
    writeCmd(0xB7);
    writeData(0x00);
    // Gate Driver Direction Control
    writeCmd(0xB8);
    writeData(0x00);

    // Display On
    writeCmd(0x29);
}

void ST7735S::colorInversion(bool inversion)
{
    writeCmd(inversion ? 0x21 : 0x20);
}

void ST7735S::sleepMode(bool on)
{
    writeCmd(on ? 0x10 : 0x11);
    delay_ms(on ? 5 : 120);
}

void ST7735S::gammaCorrect()
{
    // Enable Gamma correction
    writeCmd(0xF2);
    // Set positive Gamma correction
    writeCmd(0xE0);
    uint8_t gammaCorrectionPositive[] = {
        0x3F, 0x25, 0x1C, 0x1E, 0x20, 0x12, 0x2A, 0x90, 0x24, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    writeData(gammaCorrectionPositive, sizeof(gammaCorrectionPositive));
    // Set negative Gamma correction
    writeCmd(0xE1);
    uint8_t gammaCorrectionNegative[] = {
        0x20, 0x20, 0x20, 0x20, 0x05, 0x00, 0x15, 0xA7, 0x3D, 0x18, 0x25, 0x2A, 0x2B, 0x2B, 0x3A
    };
    writeData(gammaCorrectionNegative, sizeof(gammaCorrectionNegative));
}

uint16_t ST7735S::RGB888ToRGB565(uint32_t color)
{
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void ST7735S::fillWith(uint32_t color_rgb888)
{
    uint16_t color = RGB888ToRGB565(color_rgb888);
    uint8_t high = (color >> 8) & 0xFF;
    uint8_t low = color & 0xFF;
    size_t buf_size = 4096;
    std::vector<uint8_t> buffer(buf_size);

    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < buf_size; i += 2) {
        buffer[i] = high;
        buffer[i+1] = low;
    }
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end-start);

    std::cout << "Time spent:" << duration.count() << "μs" << std::endl;
    rangeReset();
    startWrite();
    for (uint8_t i = 0; i < 10; i++) {
        writeData(buffer.data(), buf_size);
    }
}

void ST7735S::clear()
{
    fillWith(0x000000);
}

void ST7735S::displaySwitch(bool on)
{
    writeCmd(on ? 0x29 : 0x28);
}

void ST7735S::idleMode(bool on)
{
    writeCmd(on ? 0x39 : 0x38);
}

void ST7735S::rangeSet(uint8_t xS, uint8_t xE, uint8_t yS, uint8_t yE)
{
    uint8_t xBuf[] = {0x00, xS, 0x00, xE};
    uint8_t yBuf[] = {0x00, yS, 0x00, yE};

    writeCmd(0x2A);
    writeData(xBuf, sizeof(xBuf));
    writeCmd(0x2B);
    writeData(yBuf, sizeof(yBuf));
}

void ST7735S::rangeReset()
{
    MADCTL[5] ? rangeSet(0,159,0,127) : rangeSet(0,127,0,159);
}

void ST7735S::setMADCTL()
{
    writeCmd(0x36);
    writeData(static_cast<uint8_t>(MADCTL.to_ulong()));
}

void ST7735S::refreshDirection(bool ml, bool mh)
{
    // ML=0 refresh down to up
    // ML=1 refresh up to down
    MADCTL[4] = ml;
    // MH=0 refresh left to right
    // MH=1 refresh right to left
    MADCTL[2] = mh;
    setMADCTL();
}

void ST7735S::colorOrderRGB(bool RGB)
{
    // RGB or BGR
    MADCTL[3] = !RGB;
    setMADCTL();
}

void ST7735S::orientationSet(Orientation orientation)
{
    // Memory access control
    // D7 D6 D5 D4 D3  D2 D1 D0
    // MY MX MV ML RGB MH  x  x
    switch (orientation)
    {
    case Orientation::Portrait:
        MADCTL[7] = 0;
        MADCTL[6] = 0;
        MADCTL[5] = 0;
        break;
    case Orientation::PortraitInverted:
        MADCTL[7] = 1;
        MADCTL[6] = 1;
        MADCTL[5] = 0;
        break;
    case Orientation::Landscape:
        MADCTL[7] = 0;
        MADCTL[6] = 1;
        MADCTL[5] = 1;
        break;
    case Orientation::LandscapeInverted:
        MADCTL[7] = 1;
        MADCTL[6] = 0;
        MADCTL[5] = 1;
        break;
    }
    std::cout << "MADCTL: " << MADCTL << std::endl;
    setMADCTL();
}

void ST7735S::rangeAdapt(int widthImage, int heightImage, Orientation orientation)
{
    double ratioImage = static_cast<double>(widthImage) / heightImage;
    double ratioScreenLandscape = static_cast<double>(screenHeight) / screenWidth;
    double ratioScreenPortrait = static_cast<double>(screenWidth) / screenHeight;
    std::cout << "Image: " << widthImage << "*" << heightImage << " Ratio: " << ratioImage << std::endl;
    uint8_t xS = 0, xE = 0, yS = 0, yE = 0;

    if (orientation == Orientation::Landscape || orientation == Orientation::LandscapeInverted) {
        if (ratioImage >= ratioScreenLandscape) {
            displayArea.displayWidth = screenHeight;
            displayArea.displayHeight = static_cast<int>(std::round(screenHeight / ratioImage));
        } else {
            displayArea.displayHeight = screenWidth;
            displayArea.displayWidth = static_cast<int>(std::round(screenWidth * ratioImage));
        }
        xS = static_cast<uint8_t>(std::round((screenHeight - displayArea.displayWidth) / 2));
        xE = xS + displayArea.displayWidth;
        yS = static_cast<uint8_t>(std::round((screenWidth - displayArea.displayHeight) / 2));
        yE = yS + displayArea.displayHeight;
    } else {
        if (ratioImage <= ratioScreenPortrait) {
            displayArea.displayHeight = screenHeight;
            displayArea.displayWidth = static_cast<int>(std::round(screenHeight * ratioImage));
        } else {
            displayArea.displayWidth = screenWidth;
            displayArea.displayHeight = static_cast<int>(std::round(screenWidth / ratioImage));
        }
        xS = static_cast<uint8_t>(std::round((screenWidth - displayArea.displayWidth) / 2));
        xE = xS + displayArea.displayWidth;
        yS = static_cast<uint8_t>(std::round((screenHeight - displayArea.displayHeight) / 2));
        yE = yS + displayArea.displayHeight;
    }
    orientationSet(orientation);
    rangeSet(xS, xE, yS, yE);
}

void ST7735S::imagePlay(std::string& path, Orientation orientation)
{
    clear();
    imghandler::ImageRGB565 image565;
    imghandler::ImageRGB24 image24Src;
    imghandler::ImageRGB24 image24Dst;
    imghandler::ImageType imageType = imghandler::formatProbe(path);
    switch (imageType)
    {
    case imghandler::ImageType::JPG:
        if (!imghandler::decodeJpegToRGB24(path, image24Src)) {
            std::cout << "Decode failed" << std::endl;
            return;
        }
        break;
    default:
        std::cout << "Unknown image format" << std::endl;
        break;
    }
    rangeAdapt(image24Src.width, image24Src.height, orientation);
    std::cout << "Display area: " << displayArea.displayWidth << "*" << displayArea.displayHeight << std::endl;
    if (!imghandler::scaleImage(image24Src, image24Dst, displayArea.displayWidth, displayArea.displayHeight)) {
        std::cout << "Scale failed" << std::endl;
        return;
    }
    if (!imghandler::convertToRGB565(image24Dst, image565)) {
        std::cout << "Convert failed" << std::endl;
        return;
    }
    std::cout << "image size: "<< std::dec << image565.width << " * " << image565.height << " = " << image565.data.size() << std::endl;
    startWrite();
    writeData(image565.data.data(), image565.data.size());
}

void ST7735S::testSetRange()
{
    orientationSet(Orientation::Landscape);
    rangeSet(9,59,9,119);
    size_t bufSize = 7000;
    std::vector<uint8_t> buffer(bufSize);
    for (size_t i = 0; i < bufSize; i += 2) {
        buffer[i] = 0xFF;
        buffer[i+1] = 0x00;
    }
    for (size_t i = 0; i < bufSize; i += 2) {
        buffer[i] = 0x0A;
        buffer[i+1] = 0x3C;
    }
    startWrite();
    writeData(buffer.data(), buffer.size());
}