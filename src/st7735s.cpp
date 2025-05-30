#include <st7735s.hpp>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <stdexcept>
#include <iostream>

ST7735S::ST7735S(const std::string& spi_dev, 
    const std::string& gpio_chip_name_rst, 
    const std::string& gpio_chip_name_dc, 
    const uint8_t gpio_offset_rst, 
    const uint8_t gpio_offset_dc)
{
    spi_fd = open(spi_dev.c_str(), O_RDWR);
    if (spi_fd < 0) {
        throw std::runtime_error("Failed to open SPI device" + spi_dev);
    }

    //SPI configuration
    uint32_t mode = SPI_MODE_0;
    uint32_t speed = 16000000;
    if(ioctl(spi_fd, SPI_IOC_WR_MODE, &mode) < 0) {
        throw std::runtime_error("Failed to set SPI mode");
    }
    if(ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        throw std::runtime_error("Failed to set SPI speed");
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

ST7735S::~ST7735S() {
    gpio_line_rst.release();
    gpio_line_dc.release();
    close(spi_fd);
}

void ST7735S::spiTransfer(bool isData, const uint8_t* data, size_t len) {
    gpio_line_dc.set_value(isData ? 1 : 0);
    struct spi_ioc_transfer tx = {
        .tx_buf = (uint64_t)data,
        .len = static_cast<unsigned int>(len),
        .speed_hz = 16000000,
        .delay_usecs = 0,
        .bits_per_word = 8
    };
    if (ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tx) < 0) {
        throw std::runtime_error("SPI transfer failed");
    }
}

void ST7735S::writeCommand(uint8_t cmd)
{
    spiTransfer(false, &cmd, 1);
}
void ST7735S::writeData(const uint8_t* data, size_t len)
{
    spiTransfer(true, data, len);
}