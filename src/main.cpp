#include "main.hpp"
#include "video_player.hpp"

//Pins connection: 
//  SPI: SPI3_M1 CS0
//      SPI_MOSI: 147(GPIO4_C3)
//      SPI_CLK: 146(GPIO4_C2)
//      SPI_CS0: 150(GPIO4_C6)
//  RESET: GPIO3_B0
//  D/C: GPIO3_C1

int main() {
    std::string pathImage = "../gallery/shiroko.png";
    std::string pathVideo = "../gallery/bad_apple.mp4";
    ST7735S st7735s("/dev/spidev3.0","gpiochip3",8,"gpiochip3",17);
    st7735s.init();
    // st7735s.imagePlay(pathImage, ST7735S::Orientation::LandscapeInverted);
    st7735s.reset();
    VideoPlayer player(st7735s);
    player.load(pathVideo);
}
