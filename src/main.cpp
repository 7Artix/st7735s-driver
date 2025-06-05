#include "main.hpp"
#include "video_player.hpp"

//Pins connection: 
//  SPI: SPI3_M1 CS0
//      SPI_MOSI: 147(GPIO4_C3)
//      SPI_CLK: 146(GPIO4_C2)
//      SPI_CS0: 150(GPIO4_C6)
//  RESET: GPIO3_B0
//  D/C: GPIO3_C1

int main(int argc, char* argv[]) {
    std::cout << av_gettime() << std::endl;
    if (argc < 2) {
        std::cerr << "Usage: player <video_file>" << std::endl;
        return 1;
    }

    std::string path = argv[1];

    ST7735S st7735s("/dev/spidev3.0","gpiochip3",8,"gpiochip3",17);
    st7735s.init();
    st7735s.clear();
    VideoPlayer player(st7735s, uniframe::Orientation::Landscape);
    if (!player.load(path)) {
        std::cerr << "Failed to load video" << std::endl;
        return 1;
    } else {
        std::cout << "Video load" << std::endl;
    }

    player.play();

    player.wait();
    return 0;
}
