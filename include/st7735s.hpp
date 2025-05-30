#pragma once

#include <cstdint>
#include <gpiod.hpp>
#include <string>

class ST7735S {

private:
    gpiod::line gpio_line_rst;
    gpiod::line gpio_line_dc;
    uint32_t speed = 40000000;
    int spi_fd;
    void spiTransfer(bool isData, const uint8_t* data, size_t len);
    void writeCmd(uint8_t cmd);
    void writeData(const uint8_t* data, size_t len);
    void writeData(uint8_t singleByte);
    void delay_ms(uint64_t ms);
    void gammaCorrect();
    uint16_t RGB888ToRGB565(uint32_t color);
public:
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
    void clear();
    void fillWith(uint32_t color_rgb888);
    void gpioTest();
    void reset();
    void setOrientation(Orientation orientation);
    void displaySingleFrame(uint32_t* pixels);
};