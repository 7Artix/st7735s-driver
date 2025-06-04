#pragma once

#include <cstdint>
#include <gpiod.hpp>
#include <string>
#include <bitset>
#include "image_handler.hpp"

class ST7735S {

private:
    gpiod::line gpio_line_rst;
    gpiod::line gpio_line_dc;
    // 34000000Hz
    uint32_t speed = 32000000;
    const size_t maxSPIChunkSize = 4096;
    // Memory access control
    // D7 D6 D5 D4 D3  D2 D1 D0
    // MY MX MV ML RGB MH  x  x
    std::bitset<8> MADCTL = 0b00000000;
    struct DisplayArea{int displayWidth; int displayHeight;} displayArea;
    int spi_fd;
    void spiTransfer(bool isData, const uint8_t* data, size_t len);
    void writeCmd(uint8_t cmd);
    void writeData(const uint8_t* data, size_t len);
    void writeData(uint8_t singleByte);
    void delay_ms(uint64_t ms);
    void gammaCorrect();
    void setMADCTL();
    void startWrite();
    uint16_t RGB888ToRGB565(uint32_t color);
public:
    int screenWidth = 128;
    int screenHeight = 160;
    enum class Orientation {
        Portrait,
        Landscape,
        PortraitInverted,
        LandscapeInverted
    };
    // "spi_dev" should be like: "/dev/spidev3.0"
    // "gpio_chip_*" refers to the gpiochip of the pin, should be like: "gpiochip0"
    // "gpio_offset_*" refers to the offset of the pin
    ST7735S(const std::string& spi_dev, 
        const std::string& gpio_chip_name_rst, 
        const uint8_t gpio_offset_rst, 
        const std::string& gpio_chip_name_dc,  
        const uint8_t gpio_offset_dc);
    ~ST7735S();
    void init();
    void reset();
    void colorInversion(bool inversion);
    void sleepMode(bool on);
    void displaySwitch(bool on);
    void idleMode(bool on);
    void rangeSet(uint8_t xS, uint8_t xE, uint8_t yS, uint8_t yE);
    void rangeReset();
    void rangeAdapt(int width, int height, Orientation orientation);
    void refreshDirection(bool ml, bool mh);
    void colorOrderRGB(bool RGB);
    void orientationSet(Orientation orientation);
    void fillWith(uint32_t color_rgb888);
    void clear();
    void imagePlay(std::string& path, Orientation orientation);
    void testSetRange();
};