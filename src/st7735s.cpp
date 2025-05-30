#include <st7735s.hpp>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <stdexcept>
#include <iostream>
#include <chrono>
#include <thread>

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
    spiTransfer(true, data, len);
}

void ST7735S::writeData(uint8_t singleByte)
{
    writeData(&singleByte, 1);
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
    reset();
    writeCmd(0x11); // Awake
    delay_ms(10);
    // Pixel Format
    writeCmd(0x3A);
    writeData(0x55); //RGB565
    // Gamma select
    writeCmd(0x26);
    writeData(0x04);
    gammaCorrect();
    // FPS formula: fps = 200kHz/(line+VPA[5:0])(DIVA[4:0]+4)
    // when GM = 011(128*160), line = 160
    // FPS select (normal mode, full colors)
    writeCmd(0XB1);
    writeData(0x0E);
    writeData(0x14); // fps:61.7
    // Display Inversion Control
    writeCmd(0xB4);
    writeData(0x07);
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
    // Column address range set
    uint8_t rangeCol[] = {0x00, 0x00, 0x00, 0x7F};
    writeCmd(0x2A);
    writeData(rangeCol, sizeof(rangeCol));
    // Page address range set
    uint8_t rangePage[] = {0x00, 0x00, 0x00, 0x9F};
    writeCmd(0x2B);
    writeData(rangePage, sizeof(rangePage));
    //Source Driver Direction Control
    writeCmd(0xB7);
    writeData(0x00);
    // Set orientation
    setOrientation(Orientation::Landscape);
    // Display On
    writeCmd(0x29);
    // Start receive data
    writeCmd(0x2C);
}

void ST7735S::setOrientation(Orientation orientation)
{
    // Memory access control
    // D7 D6 D5 D4 D3  D2 D1 D0
    // MY MX MV ML RGB MH x  x
    uint8_t param = 0x00;
    switch (orientation)
    {
    case Orientation::Portrait:
        param = 0xC0;
        break;
    case Orientation::Landscape:
        param = 0xA0;
        break;
    case Orientation::PortraitInverted:
        param = 0x00;
        break;
    case Orientation::LandscapeInverted:
        param = 0x60;
        break;
    }
    writeCmd(0x36);
    writeData(param);
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

void ST7735S::displaySingleFrame(uint32_t* pixels)
{
    
}

void ST7735S::fillWith(uint32_t color_rgb888)
{
    writeCmd(0x2C); // Memory write
    uint16_t color = RGB888ToRGB565(color_rgb888);
    uint8_t high = (color >> 8) & 0xFF;
    uint8_t low = color & 0xFF;
    const size_t buf_size = 4096;
    std::vector<uint8_t> buffer(buf_size);

    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < buf_size; i += 2) {
        buffer[i] = high;
        buffer[i+1] = low;
    }
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end-start);

    std::cout << "Time spent:" << duration.count() << "μs" << std::endl;

    for (uint8_t i = 0; i < 10; i++) {
        writeData(buffer.data(), buf_size);
    }
}