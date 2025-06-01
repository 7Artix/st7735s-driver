#include "main.hpp"

//Pins connection: 
//  SPI: SPI3_M1 CS0
//      SPI_MOSI: 147(GPIO4_C3)
//      SPI_CLK: 146(GPIO4_C2)
//      SPI_CS0: 150(GPIO4_C6)
//  RESET: GPIO3_B0
//  D/C: GPIO3_C1

int main() {
    std::string imagePath = "../images/bunny.jpg";
    ST7735S st7735s("/dev/spidev3.0","gpiochip3",8,"gpiochip3",17);
    st7735s.init();
    st7735s.imagePlay(imagePath, ST7735S::Orientation::Portrait);
}

void delay_ms(uint64_t ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}