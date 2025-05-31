#include "main.hpp"

//Pins connection: 
//  SPI: SPI3_M1 CS0
//      SPI_MOSI: 147(GPIO4_C3)
//      SPI_CLK: 146(GPIO4_C2)
//      SPI_CS0: 150(GPIO4_C6)
//  RESET: GPIO3_B0
//  D/C: GPIO3_C1

int main() {
    uint64_t timeDelay = 1000;
    ST7735S st7735s("/dev/spidev3.0","gpiochip3",8,"gpiochip3",17);
    st7735s.init();
    while(true) {
        st7735s.fillWith(0xFF0000);
        delay_ms(timeDelay);
        st7735s.fillWith(0x00FF00);
        delay_ms(timeDelay);
        st7735s.fillWith(0x0000FF);
        delay_ms(timeDelay);
        st7735s.fillWith(0x00FFFF);
        delay_ms(timeDelay);
        st7735s.fillWith(0xFFFF00);
        delay_ms(timeDelay);
        st7735s.fillWith(0xFF00FF);
        delay_ms(timeDelay);
        st7735s.fillWith(0xFFFFFF);
        delay_ms(timeDelay);
        st7735s.fillWith(0x000000);
        delay_ms(timeDelay);
        break;
    }
    st7735s.sleepMode(true);
}

void delay_ms(uint64_t ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}