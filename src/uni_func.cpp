#include "uni_func.hpp"

#include <iostream>
#include <chrono>
#include <thread>

namespace unifunc{

void delay_ms(uint64_t ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

}
